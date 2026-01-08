/**
 * Copyright (c) 2024-2025 Alexandr Svetlichnyi, Savva Savenkov, Artemii Novikov
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

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include <tinyxml2.h>

using namespace std::string_view_literals;

namespace {
    static const std::string FILTER_NAME_ATTRIBUTE = "name";

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

        const auto filterName = std::move(params.at(FILTER_NAME_ATTRIBUTE));
        params.erase(FILTER_NAME_ATTRIBUTE);

        // log attributes
        {
            std::cout << "filter name: " + filterName + "\nparams:\n";

            for (const auto& [name, value] : params) {
                std::cout << name << ": " << value << '\n';
            }
        }

        return factoryMap.at(filterName)->Create(params);
    }

    template <typename Target, typename Source>
    std::unique_ptr<Target> DynamicPointerCast(std::unique_ptr<Source>&& ptr) {
        Source* raw = ptr.release();
        auto* derived = dynamic_cast<Target*>(raw);
        if (derived == nullptr) {
            delete raw;
            throw std::runtime_error("DynamicPointerCast: type mismatch");
        }
        return std::unique_ptr<Target>(derived);
    }

    std::string ReadAll(std::istream& is) {
        return std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
    }
} // namespace

namespace cola {

    // converters

    AZ PdgToAZ(int pdgCode) {
        switch (pdgCode) {
            case 2112:
                return {1, 0};
            case 2212:
                return {1, 1};
            default: {
                AZ data = {0, 0};
                pdgCode /= 10;
                for (int i = 0; i < 3; i++) {
                    data.first += pdgCode % 10 * static_cast<uint16_t>(std::pow(10, i));
                    pdgCode /= 10;
                }
                for (int i = 0; i < 3; i++) {
                    data.second += pdgCode % 10 * static_cast<uint16_t>(std::pow(10, i));
                    pdgCode /= 10;
                }
                return data;
            }
        }
    }

    int AZToPdg(AZ data) {
        if (data.first == 1 && data.second == 0) {
            return 2112;
        }
        if (data.first == 1 && data.second == 1) {
            return 2212;
        }
        return 1000000000 + data.first * 10 + data.second * 10000;
    }

    AZ Particle::GetAZ() const {
        return PdgToAZ(pdgCode);
    }

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

    // Metaprocessor

    MetaProcessor::MetaProcessor(FactoryMap&& filterMap) {
        for (auto& [name, factory] : filterMap) {
            Register(std::move(factory), name);
        }
    }

    void MetaProcessor::Register(std::unique_ptr<VFactory>&& factory, const std::string& name) {
        switch (factory->GetFilterType()) {
            case FilterType::GENERATOR: {
                RegisterGenerator(std::move(factory), name);
                break;
            }
            case FilterType::CONVERTER: {
                RegisterConverter(std::move(factory), name);
                break;
            }
            case FilterType::WRITER: {
                RegisterWriter(std::move(factory), name);
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
        std::cerr << str << '\n';
        auto code = document.Parse(str.c_str());
        if (code != tinyxml2::XML_SUCCESS) {
            throw std::runtime_error("ERROR in MetaProcessor: bad XML file.\nError code (tinyxml2): " +
                                     std::to_string(code));
        }

        return BuildFilterEnsemble(document);
    }

    FilterEnsemble MetaProcessor::BuildFilterEnsemble(const tinyxml2::XMLDocument& document) const {
        using namespace tinyxml2;
        std::unique_ptr<VGenerator> generator;
        std::vector<std::unique_ptr<VConverter>> converters;
        std::unique_ptr<VWriter> writer;

        const tinyxml2::XMLElement* root = document.RootElement();
        if (root == nullptr) {
            throw std::runtime_error("Empty XML document");
        }

        for (const auto* elem = root->FirstChildElement(); elem != nullptr; elem = elem->NextSiblingElement()) {
            if (writer != nullptr) {
                throw std::runtime_error("Unexpected element after writer");
            }

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

        FilterEnsemble ensemble;
        ensemble.generator = std::move(generator);
        ensemble.converters = std::move(converters);
        ensemble.writer = std::move(writer);
        return ensemble;
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
        auto factoryMap = DoGetModuleFilters();
        if (prefix.has_value()) {
            FactoryMap prefixMap;
            for (auto& [name, factory] : factoryMap) {
                prefixMap[*prefix + "." + name] = std::move(factory);
            }
            return prefixMap;
        }

        return factoryMap;
    }

    static const std::string FUNCTION_NAME = "LoadCOLAModule";
    static const std::string ENV_DIR_VARIABLE = "COLA_DIR";
    using LoadModuleFunction = decltype(LoadCOLAModule);

    std::unique_ptr<cola::VModule> LoadModule(const std::string& moduleName,
                                              const std::optional<std::string>& libDirectory) {
        auto colaDirectory = libDirectory.value_or(
            std::getenv(ENV_DIR_VARIABLE.c_str()) != nullptr ? std::getenv(ENV_DIR_VARIABLE.c_str()) : "~/.local/lib");
        if (colaDirectory.size() > 1 && colaDirectory.substr(0, 2) == "~/") {
            colaDirectory = std::filesystem::path(std::getenv("HOME")) / colaDirectory.substr(2);
        }
        for (const auto& colaEntry : std::filesystem::directory_iterator(colaDirectory)) {
            if (!colaEntry.is_directory() || colaEntry.path().filename() != moduleName) {
                continue;
            }

            for (const auto& entry : std::filesystem::directory_iterator(colaEntry.path())) {
                if (entry.path().extension() == ".so" || entry.path().extension() == ".dylib") {
                    if (auto* libraryHandler = dlopen(entry.path().c_str(), RTLD_LAZY); libraryHandler != nullptr) {
                        if (auto* rawFunctionPtr = dlsym(libraryHandler, FUNCTION_NAME.c_str()); rawFunctionPtr != nullptr) { // nolint
                            auto* functionPtr = (LoadModuleFunction*)rawFunctionPtr;
                            return std::unique_ptr<cola::VModule>(functionPtr());
                        }

                        throw std::runtime_error("Failed to find " + FUNCTION_NAME +
                                                 " function from: " + entry.path().string());
                    }

                    throw std::runtime_error("Failed to load module: " + entry.path().string());
                }
            }
        }

        throw std::runtime_error("Failed to find module: " + moduleName + " in: " + colaDirectory);
    }

} // namespace cola
