#include "molshredder/render/volume_isosurface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"
#include "molshredder/scene/math.hpp"

namespace molshredder::render {
namespace {

using EdgeKey = std::pair<std::size_t, std::size_t>;

constexpr std::array<std::array<std::size_t, 3U>, 8U> kCornerOffsets{{
    {0U, 0U, 0U}, {1U, 0U, 0U}, {0U, 1U, 0U}, {1U, 1U, 0U},
    {0U, 0U, 1U}, {1U, 0U, 1U}, {0U, 1U, 1U}, {1U, 1U, 1U},
}};

constexpr std::array<std::array<std::size_t, 4U>, 6U> kTetrahedra{{
    {0U, 1U, 3U, 7U}, {0U, 3U, 2U, 7U}, {0U, 2U, 6U, 7U},
    {0U, 6U, 4U, 7U}, {0U, 4U, 5U, 7U}, {0U, 5U, 1U, 7U},
}};

// Generated from PyMOL 3.1.0 black-box captures of all 256 cube sign configurations
// (research session pymol-isosurface-table-blackbox_20261004T080000Z). Edge id = axis*4 + u + 2*v where u, v
// are the two other grid coordinates (in x, y, z order) of the edge's lower corner; corner bit = x + 2y + 4z.
struct CubeCase { std::uint8_t count; std::array<std::array<std::uint8_t, 3>, 5> triangles; };
constexpr std::array<CubeCase, 256> kCubeCases{{
    {0, {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 0
    {1, {{{4, 8, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 1
    {1, {{{0, 9, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 2
    {2, {{{4, 9, 5}, {8, 9, 4}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 3
    {1, {{{4, 1, 10}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 4
    {2, {{{1, 8, 0}, {10, 8, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 5
    {4, {{{0, 10, 4}, {0, 9, 10}, {1, 10, 5}, {5, 10, 9}, {0, 0, 0}}}}, // 6
    {3, {{{1, 9, 5}, {1, 10, 9}, {10, 8, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 7
    {1, {{{1, 5, 11}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 8
    {4, {{{5, 8, 0}, {5, 11, 8}, {4, 8, 1}, {1, 8, 11}, {0, 0, 0}}}}, // 9
    {2, {{{0, 11, 1}, {9, 11, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 10
    {3, {{{4, 11, 1}, {4, 8, 11}, {8, 9, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 11
    {2, {{{10, 5, 11}, {4, 5, 10}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 12
    {3, {{{5, 8, 0}, {5, 11, 8}, {11, 10, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 13
    {3, {{{0, 10, 4}, {0, 9, 10}, {9, 11, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 14
    {2, {{{10, 8, 11}, {11, 8, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 15
    {1, {{{6, 2, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 16
    {2, {{{6, 0, 4}, {2, 0, 6}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 17
    {4, {{{9, 6, 2}, {9, 5, 6}, {8, 6, 0}, {0, 6, 5}, {0, 0, 0}}}}, // 18
    {3, {{{9, 6, 2}, {9, 5, 6}, {5, 4, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 19
    {4, {{{6, 1, 10}, {6, 2, 1}, {4, 1, 8}, {8, 1, 2}, {0, 0, 0}}}}, // 20
    {3, {{{6, 1, 10}, {6, 2, 1}, {2, 0, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 21
    {5, {{{10, 6, 2}, {10, 2, 9}, {10, 9, 1}, {5, 1, 9}, {4, 0, 8}}}}, // 22
    {4, {{{6, 2, 9}, {10, 6, 9}, {10, 9, 5}, {10, 5, 1}, {0, 0, 0}}}}, // 23
    {2, {{{1, 5, 11}, {8, 6, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 24
    {5, {{{0, 11, 2}, {0, 5, 11}, {2, 11, 6}, {1, 4, 11}, {6, 11, 4}}}}, // 25
    {5, {{{0, 6, 1}, {0, 8, 6}, {1, 6, 11}, {2, 9, 6}, {11, 6, 9}}}}, // 26
    {4, {{{1, 9, 11}, {1, 6, 9}, {1, 4, 6}, {2, 9, 6}, {0, 0, 0}}}}, // 27
    {5, {{{10, 2, 11}, {10, 6, 2}, {11, 2, 5}, {8, 4, 2}, {5, 2, 4}}}}, // 28
    {4, {{{5, 11, 10}, {5, 10, 2}, {5, 2, 0}, {2, 10, 6}, {0, 0, 0}}}}, // 29
    {4, {{{4, 0, 8}, {6, 2, 10}, {10, 2, 9}, {10, 9, 11}, {0, 0, 0}}}}, // 30
    {3, {{{6, 2, 9}, {6, 9, 10}, {10, 9, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 31
    {1, {{{2, 7, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 32
    {4, {{{2, 4, 8}, {2, 7, 4}, {0, 4, 9}, {9, 4, 7}, {0, 0, 0}}}}, // 33
    {2, {{{2, 5, 0}, {7, 5, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 34
    {3, {{{2, 4, 8}, {2, 7, 4}, {7, 5, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 35
    {2, {{{4, 1, 10}, {9, 2, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 36
    {5, {{{8, 7, 10}, {8, 2, 7}, {10, 7, 1}, {9, 0, 7}, {1, 7, 0}}}}, // 37
    {5, {{{5, 10, 7}, {5, 1, 10}, {7, 10, 2}, {4, 0, 10}, {2, 10, 0}}}}, // 38
    {4, {{{1, 7, 5}, {1, 8, 7}, {1, 10, 8}, {8, 2, 7}, {0, 0, 0}}}}, // 39
    {4, {{{9, 1, 5}, {9, 2, 1}, {11, 1, 7}, {7, 1, 2}, {0, 0, 0}}}}, // 40
    {5, {{{1, 4, 8}, {1, 8, 2}, {1, 2, 11}, {7, 11, 2}, {5, 9, 0}}}}, // 41
    {3, {{{11, 2, 7}, {11, 1, 2}, {1, 0, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 42
    {4, {{{11, 2, 7}, {1, 2, 11}, {1, 8, 2}, {1, 4, 8}, {0, 0, 0}}}}, // 43
    {5, {{{5, 2, 4}, {5, 9, 2}, {4, 2, 10}, {7, 11, 2}, {10, 2, 11}}}}, // 44
    {4, {{{5, 9, 0}, {11, 8, 7}, {11, 10, 8}, {8, 2, 7}, {0, 0, 0}}}}, // 45
    {4, {{{4, 0, 2}, {4, 2, 11}, {4, 11, 10}, {7, 11, 2}, {0, 0, 0}}}}, // 46
    {3, {{{2, 7, 11}, {2, 11, 8}, {8, 11, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 47
    {2, {{{7, 8, 6}, {9, 8, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 48
    {3, {{{0, 7, 9}, {0, 4, 7}, {4, 6, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 49
    {3, {{{8, 5, 0}, {8, 6, 5}, {6, 7, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 50
    {2, {{{4, 6, 5}, {6, 7, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 51
    {5, {{{8, 1, 9}, {8, 4, 1}, {9, 1, 7}, {10, 6, 1}, {7, 1, 6}}}}, // 52
    {4, {{{10, 6, 7}, {10, 7, 0}, {10, 0, 1}, {9, 0, 7}, {0, 0, 0}}}}, // 53
    {4, {{{0, 8, 4}, {1, 10, 5}, {5, 10, 6}, {5, 6, 7}, {0, 0, 0}}}}, // 54
    {3, {{{1, 10, 6}, {1, 6, 5}, {5, 6, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 55
    {5, {{{7, 1, 6}, {7, 11, 1}, {6, 1, 8}, {5, 9, 1}, {8, 1, 9}}}}, // 56
    {4, {{{0, 5, 9}, {4, 7, 1}, {4, 6, 7}, {7, 11, 1}, {0, 0, 0}}}}, // 57
    {4, {{{8, 1, 0}, {8, 7, 1}, {8, 6, 7}, {7, 11, 1}, {0, 0, 0}}}}, // 58
    {3, {{{11, 1, 4}, {11, 4, 7}, {7, 4, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 59
    {4, {{{4, 5, 8}, {5, 9, 8}, {6, 11, 10}, {6, 7, 11}, {0, 0, 0}}}}, // 60
    {3, {{{11, 10, 6}, {11, 6, 7}, {9, 0, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 61
    {3, {{{6, 7, 11}, {6, 11, 10}, {4, 0, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 62
    {2, {{{11, 10, 6}, {7, 11, 6}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 63
    {1, {{{10, 3, 6}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 64
    {4, {{{8, 3, 6}, {8, 0, 3}, {10, 3, 4}, {4, 3, 0}, {0, 0, 0}}}}, // 65
    {2, {{{10, 3, 6}, {5, 0, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 66
    {5, {{{4, 3, 5}, {4, 10, 3}, {5, 3, 9}, {6, 8, 3}, {9, 3, 8}}}}, // 67
    {2, {{{4, 3, 6}, {1, 3, 4}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 68
    {3, {{{8, 3, 6}, {8, 0, 3}, {0, 1, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 69
    {5, {{{4, 9, 6}, {4, 0, 9}, {6, 9, 3}, {5, 1, 9}, {3, 9, 1}}}}, // 70
    {4, {{{5, 1, 3}, {5, 3, 8}, {5, 8, 9}, {6, 8, 3}, {0, 0, 0}}}}, // 71
    {4, {{{3, 5, 11}, {3, 6, 5}, {1, 5, 10}, {10, 5, 6}, {0, 0, 0}}}}, // 72
    {5, {{{0, 5, 11}, {0, 11, 3}, {0, 3, 8}, {6, 8, 3}, {4, 10, 1}}}}, // 73
    {5, {{{11, 6, 9}, {11, 3, 6}, {9, 6, 0}, {10, 1, 6}, {0, 6, 1}}}}, // 74
    {4, {{{4, 10, 1}, {8, 11, 6}, {8, 9, 11}, {11, 3, 6}, {0, 0, 0}}}}, // 75
    {3, {{{3, 5, 11}, {3, 6, 5}, {6, 4, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 76
    {4, {{{5, 11, 3}, {0, 5, 3}, {0, 3, 6}, {0, 6, 8}, {0, 0, 0}}}}, // 77
    {4, {{{3, 6, 4}, {3, 4, 9}, {3, 9, 11}, {9, 4, 0}, {0, 0, 0}}}}, // 78
    {3, {{{3, 6, 8}, {3, 8, 11}, {11, 8, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 79
    {2, {{{10, 2, 8}, {3, 2, 10}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 80
    {3, {{{10, 0, 4}, {10, 3, 0}, {3, 2, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 81
    {5, {{{2, 5, 3}, {2, 9, 5}, {3, 5, 10}, {0, 8, 5}, {10, 5, 8}}}}, // 82
    {4, {{{10, 3, 2}, {10, 2, 5}, {10, 5, 4}, {5, 2, 9}, {0, 0, 0}}}}, // 83
    {3, {{{4, 2, 8}, {4, 1, 2}, {1, 3, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 84
    {2, {{{1, 3, 0}, {0, 3, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 85
    {4, {{{4, 0, 8}, {1, 2, 5}, {1, 3, 2}, {2, 9, 5}, {0, 0, 0}}}}, // 86
    {3, {{{9, 5, 1}, {9, 1, 2}, {2, 1, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 87
    {5, {{{10, 5, 8}, {10, 1, 5}, {8, 5, 2}, {11, 3, 5}, {2, 5, 3}}}}, // 88
    {4, {{{10, 1, 4}, {3, 0, 11}, {3, 2, 0}, {0, 5, 11}, {0, 0, 0}}}}, // 89
    {4, {{{11, 3, 2}, {11, 2, 9}, {10, 1, 8}, {8, 1, 0}, {0, 0, 0}}}}, // 90
    {3, {{{3, 2, 9}, {3, 9, 11}, {1, 4, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 91
    {4, {{{8, 4, 5}, {8, 5, 3}, {8, 3, 2}, {11, 3, 5}, {0, 0, 0}}}}, // 92
    {3, {{{5, 11, 3}, {5, 3, 0}, {0, 3, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 93
    {3, {{{9, 11, 3}, {9, 3, 2}, {8, 4, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 94
    {2, {{{9, 11, 3}, {2, 9, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 95
    {4, {{{7, 10, 3}, {7, 9, 10}, {6, 10, 2}, {2, 10, 9}, {0, 0, 0}}}}, // 96
    {5, {{{4, 10, 3}, {4, 3, 7}, {4, 7, 0}, {9, 0, 7}, {8, 2, 6}}}}, // 97
    {5, {{{2, 10, 0}, {2, 6, 10}, {0, 10, 5}, {3, 7, 10}, {5, 10, 7}}}}, // 98
    {4, {{{8, 2, 6}, {10, 3, 4}, {4, 3, 7}, {4, 7, 5}, {0, 0, 0}}}}, // 99
    {5, {{{3, 9, 1}, {3, 7, 9}, {1, 9, 4}, {2, 6, 9}, {4, 9, 6}}}}, // 100
    {4, {{{8, 2, 6}, {0, 3, 9}, {0, 1, 3}, {3, 7, 9}, {0, 0, 0}}}}, // 101
    {4, {{{1, 3, 5}, {3, 7, 5}, {0, 6, 4}, {0, 2, 6}, {0, 0, 0}}}}, // 102
    {3, {{{7, 5, 1}, {7, 1, 3}, {6, 8, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 103
    {5, {{{10, 1, 5}, {10, 5, 9}, {10, 9, 6}, {2, 6, 9}, {3, 7, 11}}}}, // 104
    {4, {{{4, 10, 1}, {6, 8, 2}, {5, 9, 0}, {3, 7, 11}, {0, 0, 0}}}}, // 105
    {4, {{{11, 3, 7}, {1, 2, 10}, {1, 0, 2}, {2, 6, 10}, {0, 0, 0}}}}, // 106
    {3, {{{1, 4, 10}, {3, 7, 11}, {8, 2, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 107
    {4, {{{3, 7, 11}, {6, 5, 2}, {6, 4, 5}, {5, 9, 2}, {0, 0, 0}}}}, // 108
    {3, {{{0, 5, 9}, {2, 6, 8}, {11, 3, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 109
    {3, {{{6, 4, 0}, {6, 0, 2}, {7, 11, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 110
    {2, {{{3, 7, 11}, {6, 8, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 111
    {3, {{{7, 10, 3}, {7, 9, 10}, {9, 8, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 112
    {4, {{{0, 7, 9}, {4, 7, 0}, {4, 3, 7}, {4, 10, 3}, {0, 0, 0}}}}, // 113
    {4, {{{3, 8, 10}, {3, 5, 8}, {3, 7, 5}, {0, 8, 5}, {0, 0, 0}}}}, // 114
    {3, {{{10, 3, 7}, {10, 7, 4}, {4, 7, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 115
    {4, {{{4, 9, 8}, {4, 3, 9}, {4, 1, 3}, {3, 7, 9}, {0, 0, 0}}}}, // 116
    {3, {{{7, 9, 0}, {7, 0, 3}, {3, 0, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 117
    {3, {{{1, 3, 7}, {1, 7, 5}, {0, 8, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 118
    {2, {{{1, 3, 7}, {5, 1, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 119
    {4, {{{3, 7, 11}, {1, 5, 10}, {10, 5, 9}, {10, 9, 8}, {0, 0, 0}}}}, // 120
    {3, {{{4, 10, 1}, {5, 9, 0}, {3, 7, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 121
    {3, {{{1, 0, 8}, {1, 8, 10}, {3, 7, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 122
    {2, {{{10, 1, 4}, {3, 7, 11}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 123
    {3, {{{9, 8, 4}, {9, 4, 5}, {11, 3, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 124
    {2, {{{5, 9, 0}, {11, 3, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 125
    {2, {{{4, 0, 8}, {3, 7, 11}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 126
    {1, {{{11, 3, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 127
    {1, {{{11, 7, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 128
    {2, {{{4, 8, 0}, {3, 11, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 129
    {4, {{{7, 0, 9}, {7, 3, 0}, {5, 0, 11}, {11, 0, 3}, {0, 0, 0}}}}, // 130
    {5, {{{9, 3, 8}, {9, 7, 3}, {8, 3, 4}, {11, 5, 3}, {4, 3, 5}}}}, // 131
    {4, {{{11, 4, 1}, {11, 7, 4}, {10, 4, 3}, {3, 4, 7}, {0, 0, 0}}}}, // 132
    {5, {{{1, 7, 0}, {1, 11, 7}, {0, 7, 8}, {3, 10, 7}, {8, 7, 10}}}}, // 133
    {5, {{{4, 0, 9}, {4, 9, 7}, {4, 7, 10}, {3, 10, 7}, {1, 11, 5}}}}, // 134
    {4, {{{1, 11, 5}, {10, 9, 3}, {10, 8, 9}, {9, 7, 3}, {0, 0, 0}}}}, // 135
    {2, {{{1, 7, 3}, {5, 7, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 136
    {5, {{{1, 8, 3}, {1, 4, 8}, {3, 8, 7}, {0, 5, 8}, {7, 8, 5}}}}, // 137
    {3, {{{7, 0, 9}, {7, 3, 0}, {3, 1, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 138
    {4, {{{4, 8, 9}, {4, 9, 3}, {4, 3, 1}, {3, 9, 7}, {0, 0, 0}}}}, // 139
    {3, {{{10, 7, 3}, {10, 4, 7}, {4, 5, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 140
    {4, {{{3, 10, 8}, {3, 8, 5}, {3, 5, 7}, {0, 5, 8}, {0, 0, 0}}}}, // 141
    {4, {{{0, 9, 7}, {4, 0, 7}, {4, 7, 3}, {4, 3, 10}, {0, 0, 0}}}}, // 142
    {3, {{{7, 3, 10}, {7, 10, 9}, {9, 10, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 143
    {4, {{{2, 11, 7}, {2, 8, 11}, {3, 11, 6}, {6, 11, 8}, {0, 0, 0}}}}, // 144
    {5, {{{6, 11, 4}, {6, 3, 11}, {4, 11, 0}, {7, 2, 11}, {0, 11, 2}}}}, // 145
    {5, {{{0, 8, 6}, {0, 6, 3}, {0, 3, 5}, {11, 5, 3}, {9, 7, 2}}}}, // 146
    {4, {{{2, 9, 7}, {3, 11, 6}, {6, 11, 5}, {6, 5, 4}, {0, 0, 0}}}}, // 147
    {5, {{{1, 11, 7}, {1, 7, 2}, {1, 2, 4}, {8, 4, 2}, {10, 6, 3}}}}, // 148
    {4, {{{10, 6, 3}, {11, 7, 1}, {1, 7, 2}, {1, 2, 0}, {0, 0, 0}}}}, // 149
    {4, {{{7, 2, 9}, {1, 11, 5}, {4, 0, 8}, {6, 3, 10}, {0, 0, 0}}}}, // 150
    {3, {{{10, 6, 3}, {11, 5, 1}, {2, 9, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 151
    {5, {{{7, 8, 5}, {7, 2, 8}, {5, 8, 1}, {6, 3, 8}, {1, 8, 3}}}}, // 152
    {4, {{{0, 5, 7}, {0, 7, 2}, {1, 4, 3}, {3, 4, 6}, {0, 0, 0}}}}, // 153
    {4, {{{9, 7, 2}, {8, 6, 0}, {0, 6, 3}, {0, 3, 1}, {0, 0, 0}}}}, // 154
    {3, {{{3, 1, 4}, {3, 4, 6}, {2, 9, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 155
    {4, {{{10, 6, 3}, {4, 7, 8}, {4, 5, 7}, {7, 2, 8}, {0, 0, 0}}}}, // 156
    {3, {{{2, 0, 5}, {2, 5, 7}, {3, 10, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 157
    {3, {{{4, 0, 8}, {6, 3, 10}, {9, 7, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 158
    {2, {{{6, 3, 10}, {2, 9, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 159
    {2, {{{9, 3, 11}, {2, 3, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 160
    {5, {{{9, 4, 11}, {9, 0, 4}, {11, 4, 3}, {8, 2, 4}, {3, 4, 2}}}}, // 161
    {3, {{{5, 3, 11}, {5, 0, 3}, {0, 2, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 162
    {4, {{{8, 5, 4}, {8, 3, 5}, {8, 2, 3}, {11, 5, 3}, {0, 0, 0}}}}, // 163
    {5, {{{3, 4, 2}, {3, 10, 4}, {2, 4, 9}, {1, 11, 4}, {9, 4, 11}}}}, // 164
    {4, {{{10, 8, 3}, {8, 2, 3}, {11, 0, 1}, {11, 9, 0}, {0, 0, 0}}}}, // 165
    {4, {{{11, 5, 1}, {10, 4, 3}, {3, 4, 0}, {3, 0, 2}, {0, 0, 0}}}}, // 166
    {3, {{{10, 8, 2}, {10, 2, 3}, {11, 5, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 167
    {3, {{{9, 1, 5}, {9, 2, 1}, {2, 3, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 168
    {4, {{{5, 9, 0}, {4, 8, 1}, {1, 8, 2}, {1, 2, 3}, {0, 0, 0}}}}, // 169
    {2, {{{1, 0, 3}, {0, 2, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 170
    {3, {{{4, 8, 2}, {4, 2, 1}, {1, 2, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 171
    {4, {{{10, 2, 3}, {10, 5, 2}, {10, 4, 5}, {5, 9, 2}, {0, 0, 0}}}}, // 172
    {3, {{{2, 3, 10}, {2, 10, 8}, {0, 5, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 173
    {3, {{{10, 4, 0}, {10, 0, 3}, {3, 0, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 174
    {2, {{{10, 8, 2}, {3, 10, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 175
    {3, {{{3, 8, 6}, {3, 11, 8}, {11, 9, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 176
    {4, {{{3, 4, 6}, {3, 9, 4}, {3, 11, 9}, {9, 0, 4}, {0, 0, 0}}}}, // 177
    {4, {{{5, 3, 11}, {0, 3, 5}, {0, 6, 3}, {0, 8, 6}, {0, 0, 0}}}}, // 178
    {3, {{{3, 11, 5}, {3, 5, 6}, {6, 5, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 179
    {4, {{{6, 3, 10}, {4, 1, 8}, {8, 1, 11}, {8, 11, 9}, {0, 0, 0}}}}, // 180
    {3, {{{11, 9, 0}, {11, 0, 1}, {10, 6, 3}, {0, 0, 0}, {0, 0, 0}}}}, // 181
    {3, {{{0, 8, 4}, {1, 11, 5}, {6, 3, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 182
    {2, {{{1, 11, 5}, {10, 6, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 183
    {4, {{{5, 3, 1}, {5, 8, 3}, {5, 9, 8}, {6, 3, 8}, {0, 0, 0}}}}, // 184
    {3, {{{4, 6, 3}, {4, 3, 1}, {5, 9, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 185
    {3, {{{8, 6, 3}, {8, 3, 0}, {0, 3, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 186
    {2, {{{4, 6, 3}, {1, 4, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 187
    {3, {{{4, 5, 9}, {4, 9, 8}, {6, 3, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 188
    {2, {{{10, 6, 3}, {5, 9, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 189
    {2, {{{10, 6, 3}, {4, 0, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 190
    {1, {{{10, 6, 3}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 191
    {2, {{{11, 6, 10}, {7, 6, 11}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 192
    {5, {{{6, 0, 7}, {6, 8, 0}, {7, 0, 11}, {4, 10, 0}, {11, 0, 10}}}}, // 193
    {5, {{{11, 0, 10}, {11, 5, 0}, {10, 0, 6}, {9, 7, 0}, {6, 0, 7}}}}, // 194
    {4, {{{6, 8, 9}, {6, 9, 7}, {4, 10, 5}, {5, 10, 11}, {0, 0, 0}}}}, // 195
    {3, {{{11, 4, 1}, {11, 7, 4}, {7, 6, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 196
    {4, {{{8, 0, 1}, {8, 1, 7}, {8, 7, 6}, {7, 1, 11}, {0, 0, 0}}}}, // 197
    {4, {{{1, 11, 5}, {0, 9, 4}, {4, 9, 7}, {4, 7, 6}, {0, 0, 0}}}}, // 198
    {3, {{{7, 6, 8}, {7, 8, 9}, {5, 1, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 199
    {3, {{{1, 6, 10}, {1, 5, 6}, {5, 7, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 200
    {4, {{{1, 4, 10}, {5, 6, 0}, {5, 7, 6}, {6, 8, 0}, {0, 0, 0}}}}, // 201
    {4, {{{10, 7, 6}, {10, 0, 7}, {10, 1, 0}, {9, 7, 0}, {0, 0, 0}}}}, // 202
    {3, {{{8, 9, 7}, {8, 7, 6}, {10, 1, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 203
    {2, {{{4, 5, 6}, {6, 5, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 204
    {3, {{{8, 0, 5}, {8, 5, 6}, {6, 5, 7}, {0, 0, 0}, {0, 0, 0}}}}, // 205
    {3, {{{0, 9, 7}, {0, 7, 4}, {4, 7, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 206
    {2, {{{7, 6, 8}, {9, 7, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 207
    {3, {{{2, 11, 7}, {2, 8, 11}, {8, 10, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 208
    {4, {{{4, 2, 0}, {4, 11, 2}, {4, 10, 11}, {7, 2, 11}, {0, 0, 0}}}}, // 209
    {4, {{{7, 2, 9}, {5, 0, 11}, {11, 0, 8}, {11, 8, 10}, {0, 0, 0}}}}, // 210
    {3, {{{5, 4, 10}, {5, 10, 11}, {7, 2, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 211
    {4, {{{11, 7, 2}, {1, 11, 2}, {1, 2, 8}, {1, 8, 4}, {0, 0, 0}}}}, // 212
    {3, {{{11, 7, 2}, {11, 2, 1}, {1, 2, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 213
    {3, {{{1, 11, 5}, {0, 8, 4}, {7, 2, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 214
    {2, {{{11, 5, 1}, {7, 2, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 215
    {4, {{{1, 5, 7}, {1, 7, 8}, {1, 8, 10}, {8, 7, 2}, {0, 0, 0}}}}, // 216
    {3, {{{5, 7, 2}, {5, 2, 0}, {4, 10, 1}, {0, 0, 0}, {0, 0, 0}}}}, // 217
    {3, {{{8, 10, 1}, {8, 1, 0}, {9, 7, 2}, {0, 0, 0}, {0, 0, 0}}}}, // 218
    {2, {{{4, 10, 1}, {9, 7, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 219
    {3, {{{2, 8, 4}, {2, 4, 7}, {7, 4, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 220
    {2, {{{2, 0, 5}, {7, 2, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 221
    {2, {{{0, 8, 4}, {9, 7, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 222
    {1, {{{2, 9, 7}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 223
    {3, {{{6, 9, 2}, {6, 10, 9}, {10, 11, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 224
    {4, {{{6, 8, 2}, {10, 9, 4}, {10, 11, 9}, {9, 0, 4}, {0, 0, 0}}}}, // 225
    {4, {{{5, 10, 11}, {5, 2, 10}, {5, 0, 2}, {2, 6, 10}, {0, 0, 0}}}}, // 226
    {3, {{{10, 11, 5}, {10, 5, 4}, {8, 2, 6}, {0, 0, 0}, {0, 0, 0}}}}, // 227
    {4, {{{1, 11, 9}, {1, 9, 6}, {1, 6, 4}, {2, 6, 9}, {0, 0, 0}}}}, // 228
    {3, {{{0, 1, 11}, {0, 11, 9}, {2, 6, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 229
    {3, {{{0, 2, 6}, {0, 6, 4}, {1, 11, 5}, {0, 0, 0}, {0, 0, 0}}}}, // 230
    {2, {{{1, 11, 5}, {8, 2, 6}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 231
    {4, {{{6, 9, 2}, {10, 9, 6}, {10, 5, 9}, {10, 1, 5}, {0, 0, 0}}}}, // 232
    {3, {{{10, 1, 4}, {8, 2, 6}, {5, 9, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 233
    {3, {{{6, 10, 1}, {6, 1, 2}, {2, 1, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 234
    {2, {{{4, 10, 1}, {8, 2, 6}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 235
    {3, {{{9, 2, 6}, {9, 6, 5}, {5, 6, 4}, {0, 0, 0}, {0, 0, 0}}}}, // 236
    {2, {{{8, 2, 6}, {0, 5, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 237
    {2, {{{6, 4, 0}, {2, 6, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 238
    {1, {{{6, 8, 2}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 239
    {2, {{{10, 11, 8}, {11, 9, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 240
    {3, {{{0, 4, 10}, {0, 10, 9}, {9, 10, 11}, {0, 0, 0}, {0, 0, 0}}}}, // 241
    {3, {{{5, 0, 8}, {5, 8, 11}, {11, 8, 10}, {0, 0, 0}, {0, 0, 0}}}}, // 242
    {2, {{{10, 11, 5}, {4, 10, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 243
    {3, {{{4, 1, 11}, {4, 11, 8}, {8, 11, 9}, {0, 0, 0}, {0, 0, 0}}}}, // 244
    {2, {{{0, 1, 11}, {9, 0, 11}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 245
    {2, {{{4, 0, 8}, {1, 11, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 246
    {1, {{{1, 11, 5}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 247
    {3, {{{1, 5, 9}, {1, 9, 10}, {10, 9, 8}, {0, 0, 0}, {0, 0, 0}}}}, // 248
    {2, {{{1, 4, 10}, {5, 9, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 249
    {2, {{{1, 0, 8}, {10, 1, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 250
    {1, {{{4, 10, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 251
    {2, {{{4, 5, 9}, {8, 4, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 252
    {1, {{{0, 5, 9}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 253
    {1, {{{4, 0, 8}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 254
    {0, {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, // 255
}};

operation::Error invalid(std::string message) {
  return {operation::ErrorCode::invalid_argument, std::move(message), {}};
}

operation::Error exhausted(std::string message) {
  return {operation::ErrorCode::resource_exhausted, std::move(message),
          "increase the memory budget or use a coarser grid"};
}

operation::Error cancelled() {
  return {operation::ErrorCode::cancelled, "isosurface generation was cancelled",
          "Retry the operation when the volume is ready."};
}

bool cancellation_requested(const IsosurfaceRequest& request) noexcept {
  return request.context != nullptr &&
         request.context->cancellation.is_cancelled();
}

model::Vec3d logical_gradient(const model::VolumeGrid& grid, std::size_t x,
                              std::size_t y, std::size_t z) {
  const auto& shape = grid.shape();
  const auto derivative = [](std::size_t coordinate, std::size_t extent,
                             const auto& sample) {
    if (extent == 1U) return 0.0;
    if (coordinate == 0U) return sample(1U) - sample(0U);
    if (coordinate + 1U == extent) {
      return sample(coordinate) - sample(coordinate - 1U);
    }
    return (sample(coordinate + 1U) - sample(coordinate - 1U)) * 0.5;
  };
  return {
      derivative(x, shape.x,
                 [&](std::size_t index) { return grid.value(index, y, z); }),
      derivative(y, shape.y,
                 [&](std::size_t index) { return grid.value(x, index, z); }),
      derivative(z, shape.z,
                 [&](std::size_t index) { return grid.value(x, y, index); }),
  };
}

model::Vec3d physical_gradient(model::Vec3d logical,
                               const std::array<model::Vec3d, 3U>& deltas) {
  using scene::operator+;
  using scene::operator*;
  const auto determinant = scene::dot(deltas[0], scene::cross(deltas[1], deltas[2]));
  return scene::cross(deltas[1], deltas[2]) * (logical.x / determinant) +
         scene::cross(deltas[2], deltas[0]) * (logical.y / determinant) +
         scene::cross(deltas[0], deltas[1]) * (logical.z / determinant);
}

void orient_and_add(RenderPacket& packet, std::uint32_t first,
                    std::uint32_t second, std::uint32_t third) {
  using scene::operator+;
  using scene::operator-;
  const auto& a = packet.mesh_vertices[first];
  const auto& b = packet.mesh_vertices[second];
  const auto& c = packet.mesh_vertices[third];
  const auto face = scene::cross(b.position - a.position, c.position - a.position);
  const auto expected = a.normal + b.normal + c.normal;
  if (scene::dot(face, expected) < 0.0) std::swap(second, third);
  packet.mesh_triangles.push_back({first, second, third, 0U});
}

}  // namespace

operation::Result<RenderPacket> build_isosurface(
    const IsosurfaceRequest& request) {
  if (request.grid == nullptr) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface requires a volume grid"));
  }
  if (!std::isfinite(request.style.level)) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface level must be finite"));
  }
  if (!is_valid(request.style.color)) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface color must contain finite values in [0, 1]"));
  }
  if (request.memory_budget_bytes == 0U) {
    return operation::Result<RenderPacket>::failure(
        invalid("isosurface memory budget must be positive"));
  }
  if (cancellation_requested(request)) {
    return operation::Result<RenderPacket>::failure(cancelled());
  }

  RenderPacket packet;
  packet.scene_node_id = request.scene_node_id;
  const auto cubes = request.tessellation == IsosurfaceTessellation::marching_cubes;
  packet.provenance.emplace("algorithm",
                            cubes ? "marching-cubes" : "marching-tetrahedra");
  packet.provenance.emplace("algorithm_version", "1");
  packet.provenance.emplace("scalar_order", "x-major-y-middle-z-fastest");
  packet.provenance.emplace("level", std::to_string(request.style.level));

  const auto& grid = *request.grid;
  const auto& shape = grid.shape();
  const auto [minimum, maximum] = grid.scalars().range();
  if (shape.x < 2U || shape.y < 2U || shape.z < 2U ||
      request.style.level < minimum || request.style.level > maximum) {
    return operation::Result<RenderPacket>::success(std::move(packet));
  }

  constexpr auto kMapNodeBudgetBytes = 128U;
  constexpr auto kVertexBudgetBytes =
      sizeof(MeshVertex) * 2U + sizeof(EdgeKey) + sizeof(std::uint32_t) +
      kMapNodeBudgetBytes;
  constexpr auto kTriangleBudgetBytes = sizeof(MeshTriangle) * 2U;
  if (grid.value_count() >
      request.memory_budget_bytes / sizeof(model::Vec3d)) {
    return operation::Result<RenderPacket>::failure(
        exhausted("isosurface gradients exceed the memory budget"));
  }
  std::size_t budget_used = grid.value_count() * sizeof(model::Vec3d);
  std::vector<model::Vec3d> gradients(grid.value_count());
  for (std::size_t x = 0; x < shape.x; ++x) {
    for (std::size_t y = 0; y < shape.y; ++y) {
      for (std::size_t z = 0; z < shape.z; ++z) {
        gradients[grid.linear_index(x, y, z)] =
            physical_gradient(logical_gradient(grid, x, y, z), grid.deltas());
      }
    }
  }

  std::map<EdgeKey, std::uint32_t> edge_vertices;
  std::set<std::pair<std::uint32_t, std::uint32_t>> plane_edge_keys;
  bool budget_exhausted{};
  auto edge_vertex = [&](std::size_t first, std::size_t second) {
    const EdgeKey key = std::minmax(first, second);
    if (const auto found = edge_vertices.find(key); found != edge_vertices.end()) {
      return found->second;
    }
    const auto first_value = grid.scalars().value(first);
    const auto second_value = grid.scalars().value(second);
    const auto difference = second_value - first_value;
    const auto fraction = difference == 0.0
                              ? 0.5
                              : std::clamp((request.style.level - first_value) /
                                               difference,
                                           0.0, 1.0);
    const auto coordinate = [shape](std::size_t index) {
      const auto x = index / (shape.y * shape.z);
      const auto remainder = index % (shape.y * shape.z);
      return std::array<std::size_t, 3U>{x, remainder / shape.z,
                                         remainder % shape.z};
    };
    const auto a = coordinate(first);
    const auto b = coordinate(second);
    const auto first_position = grid.position(a[0], a[1], a[2]);
    const auto second_position = grid.position(b[0], b[1], b[2]);
    using scene::operator+;
    using scene::operator-;
    using scene::operator*;
    const auto position = first_position + (second_position - first_position) * fraction;
    const auto gradient = gradients[first] + (gradients[second] - gradients[first]) * fraction;
    const auto normal = scene::normalized(gradient * -1.0);
    if (packet.mesh_vertices.size() >=
            std::numeric_limits<std::uint32_t>::max() ||
        kVertexBudgetBytes > request.memory_budget_bytes - budget_used) {
      budget_exhausted = true;
      return std::uint32_t{};
    }
    budget_used += kVertexBudgetBytes;
    const auto index = static_cast<std::uint32_t>(packet.mesh_vertices.size());
    packet.mesh_vertices.push_back({position, normal, request.style.color});
    include(packet.bounds, position);
    edge_vertices.emplace(key, index);
    return index;
  };
  const auto add_triangle = [&](std::uint32_t first, std::uint32_t second,
                                std::uint32_t third) {
    if (budget_exhausted)
      return;
    if (kTriangleBudgetBytes > request.memory_budget_bytes - budget_used) {
      budget_exhausted = true;
      return;
    }
    budget_used += kTriangleBudgetBytes;
    orient_and_add(packet, first, second, third);
  };

  for (std::size_t x = 0; x + 1U < shape.x; ++x) {
    if (cancellation_requested(request)) {
      return operation::Result<RenderPacket>::failure(cancelled());
    }
    if (budget_exhausted) {
      return operation::Result<RenderPacket>::failure(
          exhausted("isosurface mesh exceeds the memory budget"));
    }
    if (request.context != nullptr && request.context->report_progress) {
      request.context->report_progress(
          {static_cast<double>(x) / static_cast<double>(shape.x - 1U),
           "isosurface"});
    }
    for (std::size_t y = 0; y + 1U < shape.y; ++y) {
      for (std::size_t z = 0; z + 1U < shape.z; ++z) {
        std::array<std::size_t, 8U> corners{};
        for (std::size_t corner = 0; corner < corners.size(); ++corner) {
          const auto& offset = kCornerOffsets[corner];
          corners[corner] = grid.linear_index(x + offset[0], y + offset[1],
                                               z + offset[2]);
        }
        if (cubes) {
          unsigned code = 0U;
          for (std::size_t corner = 0; corner < corners.size(); ++corner)
            if (grid.scalars().value(corners[corner]) >= request.style.level)
              code |= 1U << corner;
          const auto& entry = kCubeCases[code];
          for (std::size_t item = 0; item < entry.count; ++item) {
            std::array<std::uint32_t, 3U> face{};
            std::array<std::array<std::size_t, 3U>, 3U> faces{}; // per vertex: edge axis, fixed (axis,value) x2
            for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
              const auto edge = entry.triangles[item][vertex];
              const auto axis = static_cast<std::size_t>(edge / 4U);
              const auto u = static_cast<std::size_t>(edge % 2U);
              const auto v = static_cast<std::size_t>((edge / 2U) % 2U);
              std::array<std::size_t, 3U> low{};
              std::size_t other = 0U;
              for (std::size_t dimension = 0; dimension < 3U; ++dimension) {
                if (dimension == axis)
                  continue;
                low[dimension] = other++ == 0U ? u : v;
              }
              auto high = low;
              high[axis] = 1U;
              std::size_t first_corner = 0U;
              std::size_t second_corner = 0U;
              for (std::size_t corner = 0; corner < 8U; ++corner) {
                if (kCornerOffsets[corner] == low)
                  first_corner = corner;
                if (kCornerOffsets[corner] == high)
                  second_corner = corner;
              }
              face[vertex] = edge_vertex(corners[first_corner], corners[second_corner]);
              // The edge lies on the faces fixed at its two other coordinates.
              std::size_t slot = 0U;
              for (std::size_t dimension = 0; dimension < 3U; ++dimension) {
                if (dimension == axis)
                  continue;
                faces[vertex][slot++] = dimension * 2U + low[dimension];
              }
              faces[vertex][2] = 99U;
            }
            add_triangle(face[0], face[1], face[2]);
            if (request.plane_edges) {
              for (std::size_t side = 0; side < 3U; ++side) {
                const auto& first = faces[side];
                const auto& second = faces[(side + 1U) % 3U];
                const bool shared = first[0] == second[0] || first[0] == second[1] ||
                                    first[1] == second[0] || first[1] == second[1];
                if (!shared)
                  continue;
                auto left = face[side];
                auto right = face[(side + 1U) % 3U];
                if (right < left)
                  std::swap(left, right);
                if (left != right && plane_edge_keys.emplace(left, right).second)
                  packet.lines.push_back({packet.mesh_vertices[left].position,
                                          packet.mesh_vertices[right].position,
                                          request.style.color, request.style.color, 1.0F, 0U});
              }
            }
          }
          continue;
        }
        for (const auto& tetrahedron : kTetrahedra) {
          std::array<std::size_t, 4U> inside{};
          std::array<std::size_t, 4U> outside{};
          std::size_t inside_count = 0U;
          std::size_t outside_count = 0U;
          for (const auto local : tetrahedron) {
            const auto point = corners[local];
            if (grid.scalars().value(point) >= request.style.level) {
              inside[inside_count++] = point;
            } else {
              outside[outside_count++] = point;
            }
          }
          if (inside_count == 0U || inside_count == 4U) continue;
          if (inside_count == 1U || inside_count == 3U) {
            const auto& lone = inside_count == 1U ? inside : outside;
            const auto& others = inside_count == 1U ? outside : inside;
            const auto first = edge_vertex(lone[0], others[0]);
            const auto second = edge_vertex(lone[0], others[1]);
            const auto third = edge_vertex(lone[0], others[2]);
            add_triangle(first, second, third);
            continue;
          }
          const auto ac = edge_vertex(inside[0], outside[0]);
          const auto ad = edge_vertex(inside[0], outside[1]);
          const auto bc = edge_vertex(inside[1], outside[0]);
          const auto bd = edge_vertex(inside[1], outside[1]);
          add_triangle(ac, ad, bd);
          add_triangle(ac, bd, bc);
        }
      }
    }
  }
  if (budget_exhausted) {
    return operation::Result<RenderPacket>::failure(
        exhausted("isosurface mesh exceeds the memory budget"));
  }
  if (request.context != nullptr && request.context->report_progress) {
    request.context->report_progress({1.0, "isosurface"});
  }
  return operation::Result<RenderPacket>::success(std::move(packet));
}

}  // namespace molshredder::render
