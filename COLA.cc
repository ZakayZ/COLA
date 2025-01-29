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
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <tinyxml2.h>

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

    template <typename Base>
    std::unique_ptr<Base> DynamicPointerCast(std::unique_ptr<cola::VFilter>&& ptr) {
        cola::VFilter* raw = ptr.release();
        auto* derived = dynamic_cast<Base*>(raw);
        if (derived == nullptr) {
            delete raw;
            throw std::runtime_error("DynamicPointerCast: filter type mismatch");
        }
        return std::unique_ptr<Base>(derived);
    }
} // namespace

namespace cola {

    // converters

    AZ PdgToAz(int pdgCode) {
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

    AZ Particle::GetAz() const {
        return PdgToAz(pdgCode);
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
        XMLDocument file;
        auto code = file.LoadFile(fName.c_str());
        if (code != XML_SUCCESS) {
            throw std::runtime_error("ERROR in MetaProcessor: Couldn't open file `" + fName +
                                     "`.\nError code (tinyxml2): " + std::to_string(code));
        }

        FilterEnsemble ensemble;
        const tinyxml2::XMLNode* root = file.RootElement();
        if (root == nullptr) {
            throw std::runtime_error("ERROR in MetaProcessor: Empty XML document");
        }

        for (const tinyxml2::XMLNode* node = root->FirstChild(); node != nullptr; node = node->NextSibling()) {
            const auto* elem = node->ToElement();
            if (elem == nullptr) {
                continue;
            }

            if (ensemble.writer != nullptr) {
                throw std::runtime_error("Unexpected element after writer");
            }

            const std::string tag(elem->Name());

            if (tag == "generator") {
                if (ensemble.generator != nullptr) {
                    throw std::runtime_error("Found multiple generators");
                }
                ensemble.generator =
                    DynamicPointerCast<VGenerator>(CreateFilterFromNode(elem, generatorMap_));
                continue;
            }

            if (ensemble.generator == nullptr) {
                throw std::runtime_error("Exactly one generator must be described first");
            }

            if (tag == "converter") {
                ensemble.converters.emplace_back(
                    DynamicPointerCast<VConverter>(CreateFilterFromNode(elem, converterMap_)));
                continue;
            }

            if (tag == "writer") {
                ensemble.writer = DynamicPointerCast<VWriter>(CreateFilterFromNode(elem, writerMap_));
                continue;
            }

            throw std::runtime_error("Unknown node: " + tag);
        }

        if (ensemble.generator == nullptr) {
            throw std::runtime_error("Missing generator element");
        }
        if (ensemble.writer == nullptr) {
            throw std::runtime_error("Missing writer element");
        }

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

} // namespace cola
