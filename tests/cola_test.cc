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

#include <cstdint>
#include <memory>
#include <sstream>

#include <COLA.hh>
#include <gtest/gtest.h>

using namespace cola;

class TestGenerator : public VGenerator {
  public:
    int callCounter = 0;
    EventData referenceData = GetDefaultEventData();

    std::unique_ptr<EventData> operator()() override {
        ++callCounter;

        return std::make_unique<EventData>(referenceData);
    }

  private:
    static constexpr EventData GetDefaultEventData() {
        EventData event;
        event.iniState.pdgCodeA = 1000010020;
        event.iniState.pdgCodeB = 1000010020;
        event.iniState.pZA = 1.0;
        event.iniState.pZB = 0.0;
        event.iniState.energy = 10.0;

        Particle p1;
        p1.pdgCode = cola::AZToPdg({1, 0});
        p1.momentum = LorentzVector{.e = 1.0, .x = 0.1, .y = 0.2, .z = 0.3};
        p1.position = LorentzVector{.e = 0.0, .x = 0.0, .y = 0.0, .z = 0.0};
        p1.pClass = ParticleClass::PRODUCED;
        event.particles.push_back(p1);

        return event;
    }
};

class TestConverter : public VConverter {
  public:
    int callCounter = 0;

    std::unique_ptr<EventData> operator()(std::unique_ptr<EventData>&& data) override {
        ++callCounter;

        for (auto& particle : data->particles) {
            particle.momentum.x *= 2.0;
        }
        return data;
    }
};

class TestWriter : public VWriter {
  public:
    int callCounter = 0;
    std::vector<std::unique_ptr<EventData>> recordedEvents;

    void operator()(std::unique_ptr<EventData>&& data) override {
        ++callCounter;
        recordedEvents.emplace_back(std::move(data));
    }
};

template <typename T, typename... Args>
void AppendMakeVector(std::vector<T>& results, T&& arg, Args&&... args) {
    results.emplace_back(std::forward<T>(arg));
    if constexpr (sizeof...(args) > 0) {
        AppendMakeVector(results, std::forward<Args>(args)...);
    }
}

template <typename T, typename... Args>
std::vector<T> MakeVector(T&& arg, Args&&... args) {
    std::vector<T> results;

    AppendMakeVector(results, std::forward<T>(arg), std::forward<Args>(args)...);

    return results;
}

TEST(COLA_PDG_Test, CircularConsistency) {
    for (uint16_t atomicMass = 1; atomicMass < 100; ++atomicMass) {
        for (uint16_t chargeNumber = 0; chargeNumber <= atomicMass; ++chargeNumber) {
            auto pdgCode = cola::AZToPdg({atomicMass, chargeNumber});
            auto [circularA, circularZ] = cola::PdgToAZ(pdgCode);
            EXPECT_EQ(circularA, atomicMass) << "Failed conversion for A = " << atomicMass << ", Z = " << chargeNumber;
            EXPECT_EQ(circularZ, chargeNumber)
                << "Failed conversion for A = " << atomicMass << ", Z = " << chargeNumber;
        }
    }
}

TEST(COLA_PDG_Test, PDGToAZForNeutron) {
    auto [a, z] = PdgToAZ(2112);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(z, 0);
}

TEST(COLA_PDG_Test, PDGToAZForProton) {
    auto [a, z] = PdgToAZ(2212);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(z, 1);
}

TEST(COLA_PDG_Test, AZToPDGForNeutron) {
    auto az = AZ{1, 0};
    auto pdgCode = AZToPdg(az);
    EXPECT_EQ(pdgCode, 2112);
}

TEST(COLA_Parse, InvalidFilePath) {
    MetaProcessor processor;

    EXPECT_THROW(processor.Parse("nonexistent/file.xml"), std::runtime_error);
}

TEST(COLA_Parse, BadXMLContents) {
    auto badXMLStream = std::stringstream(R"(<?xml version="1.0"?><no end block>)");

    MetaProcessor processor;

    EXPECT_THROW(processor.Parse(badXMLStream), std::runtime_error);
}

TEST(COLA_Parse, NoGenerator) {
    auto xmlStream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <converter name="test_converter"/>
        <writer name="test_writer"/>
    </root>
    )");

    MetaProcessor processor;
    processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "test_generator");
    processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "test_converter");
    processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "test_writer");

    EXPECT_THROW(processor.Parse(xmlStream), std::runtime_error);
}

TEST(COLA_Parse, NoWriter) {
    auto xmlStream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="test_generator"/>
    </root>
    )");

    MetaProcessor processor;
    processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "test_generator");

    EXPECT_THROW(processor.Parse(xmlStream), std::runtime_error);
}

TEST(COLA_Parse, MultipleGenerators) {
    auto xmlStream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="test_generator"/>
        <generator name="test_generator"/>
        <writer name="test_writer"/>
    </root>
    )");

    MetaProcessor processor;
    processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "test_generator");
    processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "test_converter");
    processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "test_writer");

    EXPECT_THROW(processor.Parse(xmlStream), std::runtime_error);
}

TEST(COLA_Parse, MultipleWriters) {
    auto xmlStream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="test_generator"/>
        <writer name="test_writer"/>
        <writer name="test_writer"/>
    </root>
    )");

    MetaProcessor processor;
    processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "test_generator");
    processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "test_converter");
    processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "test_writer");

    EXPECT_THROW(processor.Parse(xmlStream), std::runtime_error);
}

TEST(COLA_Pipeline, RunPipeline) {
    auto ensemble = FilterEnsemble{
        .generator = std::make_unique<TestGenerator>(),
        .converters = MakeVector(std::unique_ptr<VConverter>(new TestConverter)),
        .writer = std::make_unique<TestWriter>(),
    };
    auto* generatorPtr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
    auto* converterPtr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
    auto* writerPtr = dynamic_cast<TestWriter*>(ensemble.writer.get());

    ColaRunManager manager(std::move(ensemble));
    manager.Run(3);

    EXPECT_EQ(generatorPtr->callCounter, 3);
    EXPECT_EQ(converterPtr->callCounter, 3);
    EXPECT_EQ(writerPtr->callCounter, 3);
}

TEST(ColaTest, MultipleConverters) {
    auto ensemble = FilterEnsemble{
        .generator = std::make_unique<TestGenerator>(),
        .converters =
            MakeVector(std::unique_ptr<VConverter>(new TestConverter), std::unique_ptr<VConverter>(new TestConverter)),
        .writer = std::make_unique<TestWriter>(),
    };
    auto* generatorPtr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
    auto* converterPtr1 = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
    auto* converterPtr2 = dynamic_cast<TestConverter*>(ensemble.converters[1].get());
    auto* writerPtr = dynamic_cast<TestWriter*>(ensemble.writer.get());

    ColaRunManager manager(std::move(ensemble));
    manager.Run(3);

    EXPECT_EQ(generatorPtr->callCounter, 3);
    EXPECT_EQ(converterPtr1->callCounter, 3);
    EXPECT_EQ(converterPtr2->callCounter, 3);
    EXPECT_EQ(writerPtr->callCounter, 3);
}

TEST(ColaTest, StageHandling) {
    auto ensemble = FilterEnsemble{
        .generator = std::make_unique<TestGenerator>(),
        .converters = MakeVector(std::unique_ptr<VConverter>(new TestConverter)),
        .writer = std::make_unique<TestWriter>(),
    };
    auto* generatorPtr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
    auto* converterPtr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
    auto* writerPtr = dynamic_cast<TestWriter*>(ensemble.writer.get());

    ColaRunManager manager(std::move(ensemble));
    manager.Run(1);

    EXPECT_EQ(generatorPtr->callCounter, 1);
    EXPECT_EQ(converterPtr->callCounter, 1);
    EXPECT_EQ(writerPtr->callCounter, 1);

    ASSERT_GE(writerPtr->recordedEvents.size(), 1);
    ASSERT_EQ(writerPtr->recordedEvents[0]->particles.size(), 1);
    EXPECT_EQ(writerPtr->recordedEvents[0]->particles[0].pdgCode, cola::AZToPdg({1, 0}));
}

TEST(ColaTest, ParseAndRun) {
    auto xmlStream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="test_generator"/>
        <converter name="test_converter"/>
        <writer name="test_writer"/>
    </root>
    )");

    MetaProcessor processor;
    processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "test_generator");
    processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "test_converter");
    processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "test_writer");

    auto ensemble = processor.Parse(xmlStream);

    ASSERT_EQ(ensemble.converters.size(), 1);

    auto* generatorPtr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
    auto* converterPtr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
    auto* writerPtr = dynamic_cast<TestWriter*>(ensemble.writer.get());

    ColaRunManager manager(std::move(ensemble));
    manager.Run(1);

    EXPECT_EQ(generatorPtr->callCounter, 1);
    EXPECT_EQ(converterPtr->callCounter, 1);
    EXPECT_EQ(writerPtr->callCounter, 1);

    ASSERT_GE(writerPtr->recordedEvents.size(), 1);
    ASSERT_EQ(writerPtr->recordedEvents[0]->particles.size(), 1);
    EXPECT_EQ(writerPtr->recordedEvents[0]->particles[0].pdgCode, cola::AZToPdg({1, 0}));
}
