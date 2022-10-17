#pragma once
// Runlevel
// A Runlevel is defined as a set of services (IService), which can be started.
// For more details see \file:readme.md
//



namespace Runlevel
{
  class Manager
  {
  public:
    Manager();

    Manager(const Manager& other) = delete;
    Manager& operator=(const Manager& other) = delete;

    bool demo1();
    
    int demo2(bool& demo2param);
  };

}  // namespace Runlevel
