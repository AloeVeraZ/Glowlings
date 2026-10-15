#pragma once
#include <functional>
void attachInterrupt(int,std::function<void()>,int);
