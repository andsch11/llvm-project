#pragma once

#include "annotations.hpp"
#include "demo_types.hpp"
// Runlevel
// A Runlevel is defined as a set of services (IService), which can be started.
// For more details see \file:readme.md

namespace frem {

struct Code {
  constexpr Code(unsigned){};
};
struct Alias {
  const char *alias;
};
// static constexpr ::frem::Alias<generatedVarName>("Foo");
} // namespace frem

__attribute__((annotate("AP_REFERENCE_TYPE")))
static B::AA just_for_parsing;

namespace Runlevel {
class Manager {
public:
  Manager();

  Manager(const Manager &other) = delete;
  Manager &operator=(const Manager &other) = delete;

  AP_RPC("AP_RPC(Code(0x11001100), "
         "Alias(InstrumentAPI_Motor_test_1))")
  bool demo1();

  __attribute__((annotate("AP_RPC"
                          "Code(0x11001101)"
                          "Alias(InstrumentAPI_Motor_test2)"))) int
  demo2(B::A &demo2param);

  static constexpr ::frem::Code demo3_code{123};
  void demo3(B::A &demo3param, B::AA &demoparam2);
};

} // namespace Runlevel
