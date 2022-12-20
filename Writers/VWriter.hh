//
// Created by alexsvetlichnyy on 20.12.22.
//

#ifndef GENERATORFRAMEWORK_VWRITER_HH
#define GENERATORFRAMEWORK_VWRITER_HH
#include "../Management/EventData.hh"

class VWriter {
    virtual void operator()(cola::EventData) = 0;
    virtual ~VWriter() = 0;
};

inline VWriter::~VWriter() = default;

#endif //GENERATORFRAMEWORK_VWRITER_HH
