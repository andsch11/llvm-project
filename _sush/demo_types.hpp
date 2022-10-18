#pragma once

#include "annotations.hpp"
namespace B {

struct A {
  bool struct_bool;
  int fieldmember_int;
} AP_TYPE("AP_TYPE(B:A, B_A)");

struct AA {
  bool struct_bool;
  int fieldmember_int;
};
} // namespace B
