#pragma once
// Единое объявление touch:: (реализация — src/Android_touch/Touch.cpp).
// Раньше тут лежала своя копия без pump(), из-за чего
// draw.cpp видел урезанный интерфейс.
#include "../../src/Android_touch/Touch.hpp"
