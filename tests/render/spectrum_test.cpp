#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/render/spectrum.hpp"

// Expected values: docs/research/pymol-spectrum-background-blackbox_20261002T150000Z
// (Open-Source PyMOL 3.1.0, 12 synthetic atoms).

namespace {

using namespace molshredder::render;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

constexpr std::array<float, 12> kCount{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
constexpr std::array<float, 12> kB{10, 20, 30, 40, 5, 60, 70, 80, 90, 100, 110, 120};
constexpr std::array<double, 12> kCount0{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<double, 12> kBd{10, 20, 30, 40, 5, 60, 70, 80, 90, 100, 110, 120};

struct PaletteCase {
  std::string_view palette;
  std::array<std::int32_t, 12> colors;
};

struct RangeCase {
  std::string_view palette;
  std::array<float, 12> values;
  std::optional<double> minimum;
  std::optional<double> maximum;
  std::array<std::int32_t, 12> colors;
};

template <typename Values>
struct ListCase {
  std::string_view colors;
  SpectrumInterpolation interpolation;
  Values values;
  std::array<std::int32_t, 12> expected;
};

std::vector<std::int32_t> indices(const ColorTable &table, std::string_view list) {
  std::vector<std::int32_t> result;
  std::size_t begin{};
  while (begin < list.size()) {
    auto end = list.find(' ', begin);
    if (end == std::string_view::npos)
      end = list.size();
    result.push_back(table.resolve(list.substr(begin, end - begin)).value());
    begin = end + 1U;
  }
  return result;
}

} // namespace

int main() {
  bool passed = true;
  const ColorTable table;
  passed &= expect(spectrum_palettes().size() == 60U,
                   "catalog must expose the 60 captured palettes");
  passed &= expect(find_spectrum_palette("rainbow") != nullptr &&
                       find_spectrum_palette("rainbow")->name == "rainbow" &&
                       find_spectrum_palette("blue_white_r") != nullptr &&
                       find_spectrum_palette("blue_white_r")->name == "blue_white_red" &&
                       find_spectrum_palette("blue_") == nullptr &&
                       find_spectrum_palette("no_such") == nullptr,
                   "palette lookup must accept exact names and unique prefixes");
  passed &= expect(spectrum_palette_color_list("rainbow") ==
                           "blue cyan green yellow orange red" &&
                       spectrum_palette_color_list("blue_red").empty(),
                   "value interpolation color lists must match the reference");

  const std::array<PaletteCase, 60> palettes{{
      {"blue_green", {2822, 2814, 2807, 2799, 2791, 2784, 2776, 2769, 2761, 2753, 2746, 2739}},
      {"blue_magenta", {2822, 2830, 2837, 2845, 2852, 2860, 2867, 2875, 2882, 2890, 2897, 2904}},
      {"blue_red", {2238, 2246, 2253, 2261, 2269, 2276, 2284, 2291, 2299, 2307, 2314, 2321}},
      {"blue_white_green", {3822, 3814, 3807, 3799, 3791, 3784, 3776, 3769, 3761, 3753, 3746, 3739}},
      {"blue_white_magenta", {3822, 3830, 3837, 3845, 3852, 3860, 3867, 3875, 3882, 3890, 3897, 3904}},
      {"blue_white_red", {3238, 3246, 3253, 3261, 3269, 3276, 3284, 3291, 3299, 3307, 3314, 3321}},
      {"blue_white_yellow", {3238, 3230, 3223, 3215, 3208, 3200, 3193, 3185, 3178, 3170, 3163, 3156}},
      {"blue_yellow", {2238, 2230, 2223, 2215, 2208, 2200, 2193, 2185, 2178, 2170, 2163, 2156}},
      {"cbmr", {1321, 1382, 1442, 1503, 1564, 1624, 1685, 1745, 1806, 1867, 1927, 1987}},
      {"cyan_magenta", {2572, 2564, 2557, 2549, 2541, 2534, 2526, 2519, 2511, 2503, 2496, 2489}},
      {"cyan_red", {3154, 3146, 3139, 3131, 3124, 3116, 3109, 3101, 3094, 3086, 3079, 3072}},
      {"cyan_white_magenta", {3572, 3564, 3557, 3549, 3541, 3534, 3526, 3519, 3511, 3503, 3496, 3489}},
      {"cyan_white_red", {4154, 4146, 4139, 4131, 4124, 4116, 4109, 4101, 4094, 4086, 4079, 4072}},
      {"cyan_white_yellow", {3572, 3580, 3587, 3595, 3602, 3610, 3617, 3625, 3632, 3640, 3647, 3654}},
      {"cyan_yellow", {2572, 2580, 2587, 2595, 2602, 2610, 2617, 2625, 2632, 2640, 2647, 2654}},
      {"gcbmry", {1321, 1397, 1472, 1548, 1624, 1700, 1775, 1851, 1927, 2003, 2078, 2153}},
      {"green_blue", {2738, 2746, 2753, 2761, 2769, 2776, 2784, 2791, 2799, 2807, 2814, 2821}},
      {"green_magenta", {2405, 2413, 2420, 2428, 2435, 2443, 2450, 2458, 2465, 2473, 2480, 2487}},
      {"green_red", {2405, 2397, 2390, 2382, 2375, 2367, 2360, 2352, 2345, 2337, 2330, 2323}},
      {"green_white_blue", {3738, 3746, 3753, 3761, 3769, 3776, 3784, 3791, 3799, 3807, 3814, 3821}},
      {"green_white_magenta", {3405, 3413, 3420, 3428, 3435, 3443, 3450, 3458, 3465, 3473, 3480, 3487}},
      {"green_white_red", {3405, 3397, 3390, 3382, 3375, 3367, 3360, 3352, 3345, 3337, 3330, 3323}},
      {"green_white_yellow", {3738, 3730, 3723, 3715, 3708, 3700, 3693, 3685, 3678, 3670, 3663, 3656}},
      {"green_yellow", {2738, 2730, 2723, 2715, 2708, 2700, 2693, 2685, 2678, 2670, 2663, 2656}},
      {"green_yellow_red", {655, 685, 716, 746, 776, 806, 837, 867, 897, 927, 958, 987}},
      {"magenta_blue", {2905, 2897, 2890, 2882, 2875, 2867, 2860, 2852, 2845, 2837, 2830, 2823}},
      {"magenta_cyan", {2488, 2496, 2503, 2511, 2519, 2526, 2534, 2541, 2549, 2557, 2564, 2571}},
      {"magenta_green", {2488, 2480, 2473, 2465, 2458, 2450, 2443, 2435, 2428, 2420, 2413, 2406}},
      {"magenta_white_blue", {3905, 3897, 3890, 3882, 3875, 3867, 3860, 3852, 3845, 3837, 3830, 3823}},
      {"magenta_white_cyan", {3488, 3496, 3503, 3511, 3519, 3526, 3534, 3541, 3549, 3557, 3564, 3571}},
      {"magenta_white_green", {3488, 3480, 3473, 3465, 3458, 3450, 3443, 3435, 3428, 3420, 3413, 3406}},
      {"magenta_white_yellow", {3905, 3913, 3920, 3928, 3935, 3943, 3950, 3958, 3965, 3973, 3980, 3987}},
      {"magenta_yellow", {2905, 2913, 2920, 2928, 2935, 2943, 2950, 2958, 2965, 2973, 2980, 2987}},
      {"rainbow", {4363, 4434, 4506, 4577, 4649, 4720, 4792, 4863, 4935, 5006, 5078, 5148}},
      {"rainbow2", {322, 383, 443, 504, 564, 625, 685, 746, 806, 867, 927, 987}},
      {"rainbow2_rev", {988, 927, 867, 806, 746, 685, 625, 564, 504, 443, 383, 323}},
      {"rainbow_cycle", {4256, 4347, 4438, 4528, 4619, 4710, 4801, 4892, 4983, 5073, 5164, 5254}},
      {"rainbow_cycle_rev", {5255, 5164, 5073, 4983, 4892, 4801, 4710, 4619, 4528, 4438, 4347, 4257}},
      {"rainbow_rev", {5149, 5078, 5006, 4935, 4863, 4792, 4720, 4649, 4577, 4506, 4434, 4364}},
      {"red_blue", {2322, 2314, 2307, 2299, 2291, 2284, 2276, 2269, 2261, 2253, 2246, 2239}},
      {"red_cyan", {3071, 3079, 3086, 3094, 3101, 3109, 3116, 3124, 3131, 3139, 3146, 3153}},
      {"red_green", {2322, 2330, 2337, 2345, 2352, 2360, 2367, 2375, 2382, 2390, 2397, 2404}},
      {"red_white_blue", {3322, 3314, 3307, 3299, 3291, 3284, 3276, 3269, 3261, 3253, 3246, 3239}},
      {"red_white_cyan", {4071, 4079, 4086, 4094, 4101, 4109, 4116, 4124, 4131, 4139, 4146, 4153}},
      {"red_white_green", {3322, 3330, 3337, 3345, 3352, 3360, 3367, 3375, 3382, 3390, 3397, 3404}},
      {"red_white_yellow", {3972, 3973, 3975, 3976, 3978, 3979, 3981, 3982, 3984, 3985, 3987, 3987}},
      {"red_yellow", {2972, 2973, 2975, 2976, 2978, 2979, 2981, 2982, 2984, 2985, 2987, 2987}},
      {"red_yellow_green", {988, 958, 927, 897, 867, 837, 806, 776, 746, 716, 685, 656}},
      {"rmbc", {1988, 1927, 1867, 1806, 1745, 1685, 1624, 1564, 1503, 1442, 1382, 1322}},
      {"yellow_blue", {2155, 2163, 2170, 2178, 2185, 2193, 2200, 2208, 2215, 2223, 2230, 2237}},
      {"yellow_cyan", {2655, 2647, 2640, 2632, 2625, 2617, 2610, 2602, 2595, 2587, 2580, 2573}},
      {"yellow_cyan_white", {3655, 3647, 3640, 3632, 3625, 3617, 3610, 3602, 3595, 3587, 3580, 3573}},
      {"yellow_green", {2655, 2663, 2670, 2678, 2685, 2693, 2700, 2708, 2715, 2723, 2730, 2737}},
      {"yellow_magenta", {2988, 2980, 2973, 2965, 2958, 2950, 2943, 2935, 2928, 2920, 2913, 2906}},
      {"yellow_red", {2988, 2996, 3003, 3011, 3019, 3026, 3034, 3041, 3049, 3057, 3064, 3071}},
      {"yellow_white_blue", {3155, 3163, 3170, 3178, 3185, 3193, 3200, 3208, 3215, 3223, 3230, 3237}},
      {"yellow_white_green", {3655, 3663, 3670, 3678, 3685, 3693, 3700, 3708, 3715, 3723, 3730, 3737}},
      {"yellow_white_magenta", {3988, 3980, 3973, 3965, 3958, 3950, 3943, 3935, 3928, 3920, 3913, 3906}},
      {"yellow_white_red", {3988, 3996, 4003, 4011, 4019, 4026, 4034, 4041, 4049, 4057, 4064, 4071}},
      {"yrmbcg", {2154, 2078, 2003, 1927, 1851, 1775, 1700, 1624, 1548, 1472, 1397, 1322}},
  }};
  for (const auto &item : palettes) {
    const auto *palette = find_spectrum_palette(item.palette);
    const auto mapped =
        palette == nullptr
            ? molshredder::operation::Result<PaletteSpectrumResult>::failure({})
            : palette_spectrum(kCount, std::nullopt, std::nullopt, *palette, table);
    passed &= expect(mapped.has_value() &&
                         std::equal(mapped.value().colors.begin(),
                                    mapped.value().colors.end(),
                                    item.colors.begin(), item.colors.end()) &&
                         mapped.value().range.minimum == 1.0 &&
                         mapped.value().range.maximum == 12.0,
                     std::string{"palette ramp indices drifted: "} +
                         std::string{item.palette});
  }

  const std::array<RangeCase, 5> ranges{{
      {"blue_red", kB, 30.0, 90.0, {2238, 2238, 2238, 2252, 2238, 2280, 2294, 2308, 2321, 2321, 2321, 2321}},
      {"blue_red", kB, 90.0, 30.0, {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"blue_red", kB, 50.0, std::nullopt, {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"blue_red", kB, std::nullopt, 50.0, {2242, 2249, 2256, 2264, 2238, 2278, 2285, 2293, 2300, 2307, 2315, 2321}},
      {"blue_red", kB, 50.0, 50.0, {2238, 2238, 2238, 2238, 2238, 2321, 2321, 2321, 2321, 2321, 2321, 2321}},
  }};
  for (const auto &item : ranges) {
    const auto mapped = palette_spectrum(item.values, item.minimum, item.maximum,
                                         *find_spectrum_palette(item.palette), table);
    passed &= expect(mapped.has_value() &&
                         std::equal(mapped.value().colors.begin(),
                                    mapped.value().colors.end(),
                                    item.colors.begin(), item.colors.end()),
                     "explicit/automatic range and clamping drifted");
  }
  const std::array<float, 3> constant{5.0F, 5.0F, 5.0F};
  const auto flat = palette_spectrum(constant, std::nullopt, std::nullopt,
                                     *find_spectrum_palette("blue_red"), table);
  passed &= expect(flat.has_value() && flat.value().colors ==
                                           std::vector<std::int32_t>(3U, 2238),
                   "constant values must map to the first ramp color");

  const std::array<ListCase<std::array<double, 12>>, 15> lists{{
      {"red blue", SpectrumInterpolation::rgb, kCount0, {1090453504, 1088880663, 1087373358, 1085866053, 1084358748, 1082851443, 1081278603, 1079771298, 1078263993, 1076756688, 1075249383, 1073742079}},
      {"red blue", SpectrumInterpolation::hls, kCount0, {1090453504, 1090477056, 1090500864, 1088945920, 1082916608, 1076821760, 1073807150, 1073807243, 1073807335, 1073789439, 1073765631, 1073742079}},
      {"red blue", SpectrumInterpolation::hsv, kCount0, {1090453504, 1090477056, 1090500864, 1088945920, 1082916608, 1076821760, 1073807150, 1073807243, 1073807335, 1073789439, 1073765631, 1073742079}},
      {"blue white red", SpectrumInterpolation::rgb, kCount0, {1073742079, 1076768511, 1079794943, 1082887167, 1085913599, 1088940031, 1090512871, 1090501049, 1090489227, 1090477148, 1090465326, 1090453504}},
      {"blue white red", SpectrumInterpolation::hls, kCount0, {1073742079, 1078050284, 1081794994, 1085595562, 1088677330, 1089795058, 1089794802, 1088803538, 1088400042, 1088518778, 1089225025, 1090453504}},
      {"blue white red", SpectrumInterpolation::hsv, kCount0, {1073742079, 1076807423, 1079836597, 1084293003, 1090060217, 1090515175, 1090512871, 1090501049, 1090489227, 1090477148, 1090465326, 1090453504}},
      {"salmon wheat", SpectrumInterpolation::rgb, kCount0, {1090492825, 1090428570, 1090429851, 1090431132, 1090432413, 1090368158, 1090369439, 1090370721, 1090372002, 1090307747, 1090309028, 1090310565}},
      {"salmon wheat", SpectrumInterpolation::hls, kCount0, {1090492825, 1090428570, 1090430107, 1090431388, 1090432925, 1090368670, 1090369952, 1090371233, 1090372514, 1090308259, 1090309284, 1090310565}},
      {"salmon wheat", SpectrumInterpolation::hsv, kCount0, {1090492825, 1090428570, 1090430107, 1090431388, 1090432925, 1090368670, 1090369951, 1090371233, 1090372514, 1090308259, 1090309284, 1090310565}},
      {"0xff0000 0x0000ff", SpectrumInterpolation::rgb, kCount0, {1090453504, 1088880663, 1087373358, 1085866053, 1084358748, 1082851443, 1081278603, 1079771298, 1078263993, 1076756688, 1075249383, 1073742079}},
      {"red green blue", SpectrumInterpolation::rgb, kCount0, {1090453504, 1087385088, 1084382208, 1081314048, 1078311168, 1075308288, 1073800983, 1073789253, 1073777523, 1073765538, 1073753808, 1073742079}},
      {"blue cyan green yellow orange red", SpectrumInterpolation::rgb, kBd, {1073756159, 1073784575, 1073807336, 1073807225, 1073742079, 1080295168, 1087569664, 1090510080, 1090496000, 1090481664, 1090467584, 1090453504}},
      {"blue white red", SpectrumInterpolation::rgb, kBd, {1075189503, 1078084351, 1080979199, 1083939839, 1073742079, 1089729535, 1090510301, 1090498993, 1090487685, 1090476120, 1090464812, 1090453504}},
      {"green cyan blue magenta red yellow", SpectrumInterpolation::rgb, kBd, {1073807159, 1073807270, 1073801471, 1073773055, 1073807104, 1080230143, 1087504639, 1090453692, 1090453581, 1090461952, 1090490368, 1090518784}},
      {"yellow cyan white", SpectrumInterpolation::rgb, kBd, {1089011478, 1086127938, 1083244398, 1080295323, 1090518784, 1074528243, 1075970047, 1078853631, 1081737215, 1084686335, 1087569919, 1090519039}},
  }};
  for (const auto &item : lists) {
    std::array<std::optional<double>, 12> values;
    for (std::size_t index = 0; index < values.size(); ++index)
      values[index] = item.values[index];
    const auto mapped = color_list_spectrum(values, std::nullopt, std::nullopt,
                                            indices(table, item.colors),
                                            item.interpolation, table);
    bool same = mapped.has_value();
    for (std::size_t index = 0; same && index < 12U; ++index)
      same = mapped.value().colors[index] == item.expected[index];
    passed &= expect(same, std::string{"color-list interpolation drifted: "} +
                               std::string{item.colors});
  }
  const std::array<std::optional<double>, 3> sparse{1.0, std::nullopt, 3.0};
  const auto partial = color_list_spectrum(sparse, std::nullopt, std::nullopt,
                                           indices(table, "red blue"),
                                           SpectrumInterpolation::rgb, table);
  passed &= expect(partial.has_value() && !partial.value().colors[1].has_value() &&
                       partial.value().colors[0] == 0x40FF0000 &&
                       partial.value().colors[2] == 0x400000FF,
                   "missing values must keep their atom color");
  const std::array<std::optional<double>, 2> same_values{4.0, 4.0};
  const auto zero = color_list_spectrum(same_values, std::nullopt, std::nullopt,
                                        indices(table, "salmon wheat"),
                                        SpectrumInterpolation::rgb, table);
  passed &= expect(zero.has_value() && zero.value().colors[0] == 9 &&
                       zero.value().colors[1] == 9,
                   "zero range must use the first color index");
  passed &= expect(!color_list_spectrum(same_values, std::nullopt, std::nullopt,
                                        indices(table, "red"),
                                        SpectrumInterpolation::rgb, table)
                        .has_value() &&
                       !parse_spectrum_interpolation("lab").has_value(),
                   "single colors and unknown interpolations must fail");
  return passed ? 0 : 1;
}
