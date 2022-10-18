#pragma once
// Runlevel
// A Runlevel is defined as a set of services (IService), which can be started.
// For more details see \file:readme.md

namespace B {
struct __attribute__((annotate("RPC_STRUCT(16)"))) A {
  bool struct_bool;
};
} // namespace B

namespace Runlevel {
class Manager {
public:
  Manager();

  Manager(const Manager &other) = delete;
  Manager &operator=(const Manager &other) = delete;

  bool demo1();

  int demo2(bool &demo2param);

  void demo3(B::A &demo3param);
};

} // namespace Runlevel
