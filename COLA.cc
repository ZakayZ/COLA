/**
 * Copyright (c) 2024-2026 Alexandr Svetlichnyi, Savva Savenkov, Artemii Novikov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "COLA.hh"

#include "EventData.hh"

#include <dlfcn.h>
#include <tinyxml2.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::string_view_literals;

namespace {
  const std::string filter_name_attribute = "name";

  std::unordered_map<std::string, std::string> CollectAttributes(const tinyxml2::XMLElement* element) {
    std::unordered_map<std::string, std::string> params;

    for (const auto* attribute = element->FirstAttribute(); attribute != nullptr; attribute = attribute->Next()) {
      params.emplace(attribute->Name(), attribute->Value());
    }

    return params;
  }

  std::unique_ptr<cola::VFilter> CreateFilterFromNode(
      const tinyxml2::XMLElement* element,
      const std::unordered_map<std::string, std::unique_ptr<cola::VFactory>>& factoryMap) {
    auto params = CollectAttributes(element);

    const auto filter_name = std::move(params.at(filter_name_attribute));
    params.erase(filter_name_attribute);

    // log attributes
    {
      std::cout << "filter name: " + filter_name + "\nparams:\n";

      for (const auto& [name, value] : params) {
        std::cout << name << ": " << value << '\n';
      }
    }

    return factoryMap.at(filter_name)->Create(params);
  }

  template <typename Target, typename Source>
  std::unique_ptr<Target> DynamicPointerCast(std::unique_ptr<Source>&& ptr) {
    auto result = std::unique_ptr<Target>(&dynamic_cast<Target&>(*ptr.get()));
    ptr.release();
    return result;
  }

  std::string ReadAll(std::istream& is) {
    return std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
  }
}  // namespace

namespace cola {

  // converters

  AZ Particle::GetAZ() const { return PdgToAZ(pdgCode); }

  // operators

  std::unique_ptr<EventData> operator|(const std::unique_ptr<VGenerator>& generator,
                                       const std::unique_ptr<VConverter>& converter) {
    return (*converter)((*generator)());
  }
  std::unique_ptr<EventData> operator|(std::unique_ptr<EventData>&& data,
                                       const std::unique_ptr<VConverter>& converter) {
    return (*converter)(std::move(data));
  }
  void operator|(std::unique_ptr<EventData>&& data, const std::unique_ptr<VWriter>& writer) {
    (*writer)(std::move(data));
  }

  // MetaProcessor

  MetaProcessor::MetaProcessor(FactoryMap&& filterMap) {
    for (auto& [name, factory] : filterMap) {
      Register(std::move(factory), name);
    }
  }

  void MetaProcessor::Register(std::unique_ptr<VFactory>&& factory, const std::optional<std::string>& name) {
    auto filter_name = name.value_or(factory->GetFilterName());
    switch (factory->GetFilterType()) {
      case FilterType::kGenerator: {
        RegisterGenerator(std::move(factory), filter_name);
        break;
      }
      case FilterType::kConverter: {
        RegisterConverter(std::move(factory), filter_name);
        break;
      }
      case FilterType::kWriter: {
        RegisterWriter(std::move(factory), filter_name);
        break;
      }
      default: {
        throw std::domain_error("ERROR in MetaProcessor: No such type of filter.");
      }
    }
  }

  FilterEnsemble MetaProcessor::Parse(const std::string& fName) const {
    using namespace tinyxml2;
    std::cout << "Parsing XML file:" << '\n';
    tinyxml2::XMLDocument file;
    auto code = file.LoadFile(fName.c_str());
    if (code != tinyxml2::XML_SUCCESS) {
      throw std::runtime_error("ERROR in MetaProcessor: Couldn't open file `" + fName +
                               "`.\nError code (tinyxml2): " + std::to_string(code));
    }

    return BuildFilterEnsemble(file);
  }

  FilterEnsemble MetaProcessor::Parse(std::istream& contents) const {
    tinyxml2::XMLDocument document;
    auto str = ReadAll(contents);
    auto code = document.Parse(str.c_str());
    if (code != tinyxml2::XML_SUCCESS) {
      throw std::runtime_error("ERROR in MetaProcessor: bad XML file.\nError code (tinyxml2): " + std::to_string(code));
    }

    return BuildFilterEnsemble(document);
  }

  FilterEnsemble MetaProcessor::BuildFilterEnsemble(const tinyxml2::XMLDocument& document) const {
    using namespace tinyxml2;
    std::unique_ptr<VGenerator> generator;
    std::vector<std::unique_ptr<VConverter>> converters;
    std::unique_ptr<VWriter> writer;

    for (const auto* elem = document.RootElement()->FirstChildElement(); elem != nullptr;
         elem = elem->NextSiblingElement()) {
      if (elem->Name() == "generator"sv) {
        if (generator != nullptr) {
          throw std::runtime_error("Found multiple generators");
        }
        generator = DynamicPointerCast<VGenerator>(CreateFilterFromNode(elem, generatorMap_));
        continue;
      }
      if (generator == nullptr) {
        throw std::runtime_error("Exactly one generator must be described first");
      }
      if (writer != nullptr) {
        throw std::runtime_error("Exactly one writer must be described last");
      }

      if (elem->Name() == "converter"sv) {
        converters.emplace_back(DynamicPointerCast<VConverter>(CreateFilterFromNode(elem, converterMap_)));
        continue;
      }

      if (elem->Name() == "writer"sv) {
        writer = DynamicPointerCast<VWriter>(CreateFilterFromNode(elem, writerMap_));
        continue;
      }

      throw std::runtime_error(std::string("Unknown node: ") + elem->Name());
    }

    if (generator == nullptr) {
      throw std::runtime_error("No generator found");
    }

    if (writer == nullptr) {
      throw std::runtime_error("No writer found");
    }

    return {
        .generator = std::move(generator),
        .converters = std::move(converters),
        .writer = std::move(writer),
    };
  }

  // Run manager

  void ColaRunManager::Run(int n) const {
    for (auto k = 0; k < n; ++k) {
      auto event = (*(filterEnsemble_.generator))();
      for (const auto& converter : filterEnsemble_.converters) {
        event = std::move(event) | converter;
      }
      std::move(event) | filterEnsemble_.writer;
    }
  }

  // COLA module

  FactoryMap VModule::GetModuleFilters(const std::optional<std::string>& prefix) const {
    auto factory_map = DoGetModuleFilters();
    if (prefix.has_value()) {
      FactoryMap prefix_map;
      for (auto& [name, factory] : factory_map) {
        prefix_map[*prefix + "." + name] = std::move(factory);
      }
      return prefix_map;
    }

    return factory_map;
  }

  static const std::string function_name = "LoadCOLAModule";
  static const std::string env_dir_variable = "COLA_DIR";
  using LoadModuleFunction = decltype(LoadCOLAModule);

  std::unique_ptr<cola::VModule> LoadModule(const std::string& moduleName,
                                            const std::optional<std::string>& libDirectory) {
    auto cola_directory = libDirectory.value_or(
        std::getenv(env_dir_variable.c_str()) != nullptr ? std::getenv(env_dir_variable.c_str()) : "~/.local/lib");
    if (cola_directory.size() > 1 && cola_directory.substr(0, 2) == "~/") {
      cola_directory = std::filesystem::path(std::getenv("HOME")) / cola_directory.substr(2);
    }
    for (const auto& cola_entry : std::filesystem::directory_iterator(cola_directory)) {
      if (!cola_entry.is_directory() || cola_entry.path().filename() != moduleName) {
        continue;
      }

      for (const auto& entry : std::filesystem::directory_iterator(cola_entry.path())) {
        if (entry.path().extension() == ".so" || entry.path().extension() == ".dylib") {
          if (auto* library_handler = dlopen(entry.path().c_str(), RTLD_LAZY); library_handler != nullptr) {
            if (auto* raw_function_ptr = dlsym(library_handler, function_name.c_str()); raw_function_ptr != nullptr) {
              auto* function_ptr = reinterpret_cast<LoadModuleFunction*>(raw_function_ptr);
              return std::unique_ptr<cola::VModule>(function_ptr());
            }

            throw std::runtime_error("Failed to find " + function_name + " function from: " + entry.path().string());
          }

          throw std::runtime_error("Failed to load module: " + entry.path().string());
        }
      }
    }

    throw std::runtime_error("Failed to find module: " + moduleName + " in: " + cola_directory);
  }

}  // namespace cola
