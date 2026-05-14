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

#include <COLA.hh>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>

using namespace cola;

namespace {

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

}  // namespace

class TestGenerator : public VGenerator {
 public:
  static inline const std::string kName = "TestGenerator";

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
    p1.pClass = ParticleClass::kProduced;
    event.particles.push_back(p1);

    return event;
  }
};

class TestConverter : public VConverter {
 public:
  static inline const std::string kName = "TestConverter";

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
  static inline const std::string kName = "TestWriter";

  int callCounter = 0;
  std::vector<std::unique_ptr<EventData>> recordedEvents;

  void operator()(std::unique_ptr<EventData>&& data) override {
    ++callCounter;
    recordedEvents.emplace_back(std::move(data));
  }
};

TEST(ColaPdgTest, CircularConsistency) {
  for (uint16_t atomic_mass = 1; atomic_mass < 100; ++atomic_mass) {
    for (uint16_t charge_number = 0; charge_number <= atomic_mass; ++charge_number) {
      auto pdg_code = cola::AZToPdg({atomic_mass, charge_number});
      auto [circularA, circularZ] = cola::PdgToAZ(pdg_code);
      EXPECT_EQ(circularA, atomic_mass) << "Failed conversion for A = " << atomic_mass << ", Z = " << charge_number;
      EXPECT_EQ(circularZ, charge_number) << "Failed conversion for A = " << atomic_mass << ", Z = " << charge_number;
    }
  }
}

TEST(ColaPdgTest, PDGToAZForNeutron) {
  auto [a, z] = PdgToAZ(2112);
  EXPECT_EQ(a, 1);
  EXPECT_EQ(z, 0);
}

TEST(ColaPdgTest, PDGToAZForProton) {
  auto [a, z] = PdgToAZ(2212);
  EXPECT_EQ(a, 1);
  EXPECT_EQ(z, 1);
}

TEST(ColaPdgTest, AZToPDGForNeutron) {
  auto az = AZ{1, 0};
  auto pdg_code = AZToPdg(az);
  EXPECT_EQ(pdg_code, 2112);
}

TEST(ColaParse, InvalidFilePath) {
  MetaProcessor processor;

  EXPECT_THROW(processor.Parse("nonexistent/file.xml"), std::runtime_error);
}

TEST(ColaParse, BadXMLContents) {
  auto bad_xml_stream = std::stringstream(R"(<?xml version="1.0"?><no end block>)");

  MetaProcessor processor;

  EXPECT_THROW(processor.Parse(bad_xml_stream), std::runtime_error);
}

TEST(ColaParse, NoGenerator) {
  auto xml_stream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <converter name="TestConverter"/>
        <writer name="TestWriter"/>
    </root>
    )");

  MetaProcessor processor;
  processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "TestGenerator");
  processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "TestConverter");
  processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "TestWriter");

  EXPECT_THROW(processor.Parse(xml_stream), std::runtime_error);
}

TEST(ColaParse, NoWriter) {
  auto xml_stream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="TestGenerator"/>
    </root>
    )");

  MetaProcessor processor;
  processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "TestGenerator");

  EXPECT_THROW(processor.Parse(xml_stream), std::runtime_error);
}

TEST(ColaParse, MultipleGenerators) {
  auto xml_stream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="TestGenerator"/>
        <generator name="TestGenerator"/>
        <writer name="TestWriter"/>
    </root>
    )");

  MetaProcessor processor;
  processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "TestGenerator");
  processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "TestConverter");
  processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "TestWriter");

  EXPECT_THROW(processor.Parse(xml_stream), std::runtime_error);
}

TEST(ColaParse, MultipleWriters) {
  auto xml_stream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="TestGenerator"/>
        <writer name="TestWriter"/>
        <writer name="TestWriter"/>
    </root>
    )");

  MetaProcessor processor;
  processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "TestGenerator");
  processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "TestConverter");
  processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "TestWriter");

  EXPECT_THROW(processor.Parse(xml_stream), std::runtime_error);
}

TEST(ColaPipeline, RunPipeline) {
  auto ensemble = FilterEnsemble{
      .generator = std::make_unique<TestGenerator>(),
      .converters = MakeVector(std::unique_ptr<VConverter>(new TestConverter)),
      .writer = std::make_unique<TestWriter>(),
  };
  auto* generator_ptr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
  auto* converter_ptr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
  auto* writer_ptr = dynamic_cast<TestWriter*>(ensemble.writer.get());

  ColaRunManager manager(std::move(ensemble));
  manager.Run(3);

  EXPECT_EQ(generator_ptr->callCounter, 3);
  EXPECT_EQ(converter_ptr->callCounter, 3);
  EXPECT_EQ(writer_ptr->callCounter, 3);
}

TEST(ColaTest, MultipleConverters) {
  auto ensemble = FilterEnsemble{
      .generator = std::make_unique<TestGenerator>(),
      .converters =
          MakeVector(std::unique_ptr<VConverter>(new TestConverter), std::unique_ptr<VConverter>(new TestConverter)),
      .writer = std::make_unique<TestWriter>(),
  };
  auto* generator_ptr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
  auto* converter_ptr1 = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
  auto* converter_ptr2 = dynamic_cast<TestConverter*>(ensemble.converters[1].get());
  auto* writer_ptr = dynamic_cast<TestWriter*>(ensemble.writer.get());

  ColaRunManager manager(std::move(ensemble));
  manager.Run(3);

  EXPECT_EQ(generator_ptr->callCounter, 3);
  EXPECT_EQ(converter_ptr1->callCounter, 3);
  EXPECT_EQ(converter_ptr2->callCounter, 3);
  EXPECT_EQ(writer_ptr->callCounter, 3);
}

TEST(ColaTest, StageHandling) {
  auto ensemble = FilterEnsemble{
      .generator = std::make_unique<TestGenerator>(),
      .converters = MakeVector(std::unique_ptr<VConverter>(new TestConverter)),
      .writer = std::make_unique<TestWriter>(),
  };
  auto* generator_ptr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
  auto* converter_ptr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
  auto* writer_ptr = dynamic_cast<TestWriter*>(ensemble.writer.get());

  ColaRunManager manager(std::move(ensemble));
  manager.Run(1);

  EXPECT_EQ(generator_ptr->callCounter, 1);
  EXPECT_EQ(converter_ptr->callCounter, 1);
  EXPECT_EQ(writer_ptr->callCounter, 1);

  ASSERT_GE(writer_ptr->recordedEvents.size(), 1);
  ASSERT_EQ(writer_ptr->recordedEvents[0]->particles.size(), 1);
  EXPECT_EQ(writer_ptr->recordedEvents[0]->particles[0].pdgCode, cola::AZToPdg({1, 0}));
}

TEST(ColaTest, ParseAndRun) {
  auto xml_stream = std::stringstream(R"(<?xml version="1.0"?>
    <root>
        <generator name="TestGenerator"/>
        <converter name="TestConverter"/>
        <writer name="TestWriter"/>
    </root>
    )");

  MetaProcessor processor;
  processor.Register(std::make_unique<GenericFactory<TestGenerator>>(), "TestGenerator");
  processor.Register(std::make_unique<GenericFactory<TestConverter>>(), "TestConverter");
  processor.Register(std::make_unique<GenericFactory<TestWriter>>(), "TestWriter");

  auto ensemble = processor.Parse(xml_stream);

  ASSERT_EQ(ensemble.converters.size(), 1);

  auto* generator_ptr = dynamic_cast<TestGenerator*>(ensemble.generator.get());
  auto* converter_ptr = dynamic_cast<TestConverter*>(ensemble.converters[0].get());
  auto* writer_ptr = dynamic_cast<TestWriter*>(ensemble.writer.get());

  ColaRunManager manager(std::move(ensemble));
  manager.Run(1);

  EXPECT_EQ(generator_ptr->callCounter, 1);
  EXPECT_EQ(converter_ptr->callCounter, 1);
  EXPECT_EQ(writer_ptr->callCounter, 1);

  ASSERT_GE(writer_ptr->recordedEvents.size(), 1);
  ASSERT_EQ(writer_ptr->recordedEvents[0]->particles.size(), 1);
  EXPECT_EQ(writer_ptr->recordedEvents[0]->particles[0].pdgCode, cola::AZToPdg({1, 0}));
}
