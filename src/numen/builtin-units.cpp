#include "builtin-units.hpp"
#include <array>
#include <chrono>

namespace {
constexpr double toSeconds(auto d) { return std::chrono::duration<double>(d).count(); }
} // namespace

// Aliases must be unique within a dimension; collisions across dimensions are
// resolved from context at evaluation time. Lookup is case-insensitive and
// strips plurals / expands SI and binary prefixes on `prefixable` bases, so
// rows only need irregular forms (feet, micron) and colloquial shadows that
// override expansion (mb = megabyte, not millibit).
//
// Factors are relative to a per-dimension base unit.
// Sources: NIST SP 811 (exact defined conversions), ISO 4217 (currency codes).
std::span<const UnitDef> units::builtins() {
  // clang-format off
  static const auto BUILTIN_UNITS = std::to_array<UnitDef>({
    // length
    {.id = "meter",      .aliases = {"m", "metre"}, .symbol = "m",  .factor = 1,        .dimension = dimensions::LENGTH, .family = families::METRIC, .prefixable = true},
    {.id = "micrometer", .aliases = {"micron"},     .symbol = "µm", .factor = 1e-6,     .dimension = dimensions::LENGTH, .family = families::METRIC},
    {.id = "inch",       .aliases = {"in", "\""},         .symbol = "in", .factor = 0.0254,   .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "foot",       .aliases = {"ft", "feet", "'"}, .symbol = "ft", .factor = 0.3048,   .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "yard",       .aliases = {"yd"},         .symbol = "yd", .factor = 0.9144,   .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "mile",       .aliases = {"mi"},         .symbol = "mi", .factor = 1609.344, .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "nauticalmile", .aliases = {"nmi"},                .symbol = "nmi", .name = "nautical mile", .factor = 1852,                   .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "fathom",       .aliases = {"ftm"},                .symbol = "ftm",                          .factor = 1.8288,                 .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "furlong",      .aliases = {"fur"},                .symbol = "fur",                          .factor = 201.168,                .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "hand",         .aliases = {},                     .symbol = "hh",                           .factor = 0.1016,                 .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "thou",         .aliases = {},                     .symbol = "thou",                         .factor = 2.54e-5,                .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "angstrom",     .aliases = {"ångström", "Å"},      .symbol = "Å",                            .factor = 1e-10,                  .dimension = dimensions::LENGTH, .family = families::METRIC},
    {.id = "rackunit",     .aliases = {"ru"},                 .symbol = "U",   .name = "rack unit",     .factor = 0.04445,                .dimension = dimensions::LENGTH, .family = families::IMPERIAL},
    {.id = "astronomicalunit", .aliases = {"au"},             .symbol = "au",  .name = "astronomical unit", .factor = 149597870700.0,     .dimension = dimensions::LENGTH, .family = families::METRIC},
    {.id = "lightyear",    .aliases = {"ly"},                 .symbol = "ly",  .name = "light-year",    .factor = 9460730472580800.0,     .dimension = dimensions::LENGTH, .family = families::METRIC},
    {.id = "parsec",       .aliases = {},                                                             .factor = 3.0856775814913673e16,  .dimension = dimensions::LENGTH, .family = families::METRIC},
    // css absolute units: 1in = 72pt = 6pc = 96 reference px, not device px
    {.id = "point", .aliases = {"pt"}, .symbol = "pt", .factor = 0.0254 / 72, .dimension = dimensions::LENGTH, .family = families::TYPOGRAPHIC},
    {.id = "pica",  .aliases = {"pc"}, .symbol = "pc", .factor = 0.0254 / 6,  .dimension = dimensions::LENGTH, .family = families::TYPOGRAPHIC},
    {.id = "pixel", .aliases = {"px"}, .symbol = "px", .factor = 0.0254 / 96, .dimension = dimensions::LENGTH, .family = families::TYPOGRAPHIC},
    {.id = "rem", .aliases = {},       .symbol = "rem", .factor = 0.0254 / 96 * 16, .dimension = dimensions::LENGTH, .family = families::TYPOGRAPHIC, .implicitTarget = "pixel"},
    {.id = "em",  .aliases = {},       .symbol = "em",  .factor = 0.0254 / 96 * 16, .dimension = dimensions::LENGTH, .family = families::TYPOGRAPHIC, .implicitTarget = "pixel"},

    // mass
    {.id = "gram",     .aliases = {"g", "gm", "gramme"}, .symbol = "g",  .factor = 1,            .dimension = dimensions::MASS, .family = families::METRIC, .prefixable = true},
    {.id = "kilogram", .aliases = {"kg", "kilo"},        .symbol = "kg", .factor = 1e3,          .dimension = dimensions::MASS, .family = families::METRIC},
    {.id = "tonne",    .aliases = {"t", "ton"},          .symbol = "t",  .factor = 1e6,          .dimension = dimensions::MASS, .family = families::METRIC},
    {.id = "pound",    .aliases = {"lb"},                .symbol = "lb", .factor = 453.59237,    .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "ounce",    .aliases = {"oz"},                .symbol = "oz", .factor = 28.349523125, .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "stone",    .aliases = {"st"},                .symbol = "st", .factor = 6350.29318,   .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "grain",    .aliases = {},                    .symbol = "grain", .factor = 0.06479891,   .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "dram",     .aliases = {"dr"},                .symbol = "dr", .factor = 1.7718451953125, .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "slug",     .aliases = {},                    .symbol = "slug", .factor = 14593.90294, .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "shortton", .aliases = {"uston"},             .symbol = "short ton", .name = "short ton", .factor = 907184.74,   .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "longton",  .aliases = {"ukton"},             .symbol = "long ton", .name = "long ton",  .factor = 1016046.9088, .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "carat",    .aliases = {"ct"},                .symbol = "ct", .factor = 0.2,          .dimension = dimensions::MASS, .family = families::METRIC},
    {.id = "troyounce", .aliases = {"ozt", "oz_t"},      .symbol = "ozt", .name = "troy ounce", .factor = 31.1034768, .dimension = dimensions::MASS, .family = families::IMPERIAL},
    {.id = "dalton",   .aliases = {"amu"},               .symbol = "Da", .factor = 1.66053906660e-24, .dimension = dimensions::MASS, .family = families::METRIC},

    // duration
    {.id = "second", .aliases = {"s", "sec"}, .symbol = "s",   .factor = toSeconds(std::chrono::seconds{1}), .dimension = dimensions::DURATION, .family = families::DURATION, .prefixable = true},
    {.id = "minute", .aliases = {"m", "min"}, .symbol = "min", .factor = toSeconds(std::chrono::minutes{1}), .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "hour",   .aliases = {"h", "hr"},  .symbol = "h",   .factor = toSeconds(std::chrono::hours{1}),   .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "day",    .aliases = {"d"},        .symbol = "d",   .factor = toSeconds(std::chrono::days{1}),    .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "week",   .aliases = {"wk"},       .symbol = "wk",  .factor = toSeconds(std::chrono::weeks{1}),   .dimension = dimensions::DURATION, .family = families::DURATION},
    // average month/year
    {.id = "month", .aliases = {"mo"}, .symbol = "mo", .factor = toSeconds(std::chrono::months{1}), .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "year",  .aliases = {"yr"}, .symbol = "yr", .factor = toSeconds(std::chrono::years{1}),  .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "fortnight",  .aliases = {}, .symbol = "fortnight",  .factor = toSeconds(std::chrono::weeks{2}),       .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "decade",     .aliases = {}, .symbol = "decade",     .factor = toSeconds(std::chrono::years{10}),      .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "century",    .aliases = {"centuries"}, .symbol = "century", .factor = toSeconds(std::chrono::years{100}), .dimension = dimensions::DURATION, .family = families::DURATION},
    {.id = "millennium", .aliases = {"millennia"}, .symbol = "millennium", .factor = toSeconds(std::chrono::years{1000}), .dimension = dimensions::DURATION, .family = families::DURATION},

    // temperature
    {.id = "kelvin",     .aliases = {"k"},                      .symbol = "K",  .factor = 1,       .dimension = dimensions::TEMPERATURE, .family = families::DEGREE},
    {.id = "celsius",    .aliases = {"c", "cel", "centigrade"}, .symbol = "°C", .factor = 1,       .dimension = dimensions::TEMPERATURE, .family = families::DEGREE, .offset = 273.15},
    {.id = "fahrenheit", .aliases = {"f", "fahren"},            .symbol = "°F", .factor = 5.0 / 9, .dimension = dimensions::TEMPERATURE, .family = families::DEGREE, .offset = 459.67 * 5 / 9},
    {.id = "rankine",    .aliases = {"ra"},                     .symbol = "°R", .factor = 5.0 / 9, .dimension = dimensions::TEMPERATURE, .family = families::DEGREE},

    // data: decimal SI sizes; binary sizes come from the ki/mi/gi/ti prefixes.
    // kb..pb shadow prefix expansion so "mb" stays megabyte, never millibit.
    {.id = "byte",     .aliases = {"b"},         .symbol = "B",   .factor = 1,     .dimension = dimensions::DATA, .family = families::DATA, .prefixable = true},
    {.id = "bit",      .aliases = {},            .symbol = "bit", .factor = 0.125, .dimension = dimensions::DATA, .family = families::DATA, .prefixable = true},
    {.id = "nibble",   .aliases = {"nybble"},    .symbol = "nibble", .factor = 0.5, .dimension = dimensions::DATA, .family = families::DATA},
    {.id = "kilobyte", .aliases = {"kb"},        .symbol = "kB",  .factor = 1e3,   .dimension = dimensions::DATA, .family = families::DATA},
    {.id = "megabyte", .aliases = {"mb", "meg"}, .symbol = "MB",  .factor = 1e6,   .dimension = dimensions::DATA, .family = families::DATA},
    {.id = "gigabyte", .aliases = {"gb", "gig"}, .symbol = "GB",  .factor = 1e9,   .dimension = dimensions::DATA, .family = families::DATA},
    {.id = "terabyte", .aliases = {"tb"},        .symbol = "TB",  .factor = 1e12,  .dimension = dimensions::DATA, .family = families::DATA},
    {.id = "petabyte", .aliases = {"pb"},        .symbol = "PB",  .factor = 1e15,  .dimension = dimensions::DATA, .family = families::DATA},

    // volume
    {.id = "liter", .aliases = {"l", "litre"}, .symbol = "L", .factor = 1e-3, .dimension = dimensions::VOLUME, .family = families::METRIC, .prefixable = true},
    // US customary
    {.id = "gallon",     .aliases = {"gal"},  .symbol = "gal",  .factor = 3.785411784e-3,      .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "quart",      .aliases = {"qt"},   .symbol = "qt",   .factor = 0.946352946e-3,      .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "pint",       .aliases = {"pt"},   .symbol = "pt",   .factor = 0.473176473e-3,      .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "cup",        .aliases = {},       .symbol = "cup",  .factor = 0.2365882365e-3,     .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "tablespoon", .aliases = {"tbsp"}, .symbol = "tbsp", .factor = 0.01478676478125e-3, .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "teaspoon",   .aliases = {"tsp"},  .symbol = "tsp",  .factor = 0.00492892159375e-3, .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "fluidounce", .aliases = {"floz", "fl_oz", "oz"}, .symbol = "fl oz", .name = "fluid ounce", .factor = 0.0295735295625e-3, .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "gill",       .aliases = {"gi"},   .symbol = "gi",   .factor = 0.11829411825e-3,    .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "barrel",     .aliases = {"bbl"},  .symbol = "bbl",  .factor = 158.987294928e-3,    .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "bushel",     .aliases = {"bu"},   .symbol = "bu",   .factor = 35.23907016688e-3,   .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    // imperial (UK)
    {.id = "ukgallon",     .aliases = {"ukgal", "gal_uk", "imperialgallon"},    .symbol = "UK gal",   .name = "imperial gallon",      .factor = 4.54609e-3,         .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "ukquart",      .aliases = {"ukqt", "qt_uk", "imperialquart"},       .symbol = "UK qt",    .name = "imperial quart",       .factor = 1.1365225e-3,       .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "ukpint",       .aliases = {"ukpt", "pt_uk", "imperialpint"},        .symbol = "UK pt",    .name = "imperial pint",        .factor = 0.56826125e-3,      .dimension = dimensions::VOLUME, .family = families::IMPERIAL},
    {.id = "ukfluidounce", .aliases = {"ukfloz", "fl_oz_uk", "imperialfluidounce"}, .symbol = "UK fl oz", .name = "imperial fluid ounce", .factor = 0.0284130625e-3, .dimension = dimensions::VOLUME, .family = families::IMPERIAL},

    // area
    {.id = "hectare", .aliases = {"ha"}, .symbol = "ha", .factor = 1e4,          .dimension = dimensions::AREA, .family = families::METRIC},
    {.id = "acre",    .aliases = {},     .symbol = "ac", .factor = 4046.8564224, .dimension = dimensions::AREA, .family = families::IMPERIAL},
    {.id = "are",     .aliases = {},     .symbol = "are", .factor = 1e2,          .dimension = dimensions::AREA, .family = families::METRIC},

    // speed
    {.id = "knot", .aliases = {"kt"}, .symbol = "kn", .factor = 1852.0 / 3600, .dimension = dimensions::SPEED, .family = families::IMPERIAL},
    {.id = "mach", .aliases = {},     .symbol = "Ma", .factor = 340.294,       .dimension = dimensions::SPEED, .family = families::METRIC},

    // force. the mass base is the gram, so coherent SI factors carry a 1e3
    {.id = "newton",     .aliases = {"n"},   .symbol = "N",   .factor = 1e3,                .dimension = dimensions::FORCE, .family = families::SI, .prefixable = true},
    {.id = "dyne",       .aliases = {"dyn"}, .symbol = "dyn", .factor = 1e-2,               .dimension = dimensions::FORCE, .family = families::SI},
    {.id = "poundforce", .aliases = {"lbf"}, .symbol = "lbf", .name = "pound-force",    .factor = 4448.2216152605,    .dimension = dimensions::FORCE, .family = families::IMPERIAL},
    {.id = "kilogramforce", .aliases = {"kgf", "kilopond", "kp"}, .symbol = "kgf", .name = "kilogram-force", .factor = 9806.65, .dimension = dimensions::FORCE, .family = families::SI},
    // pressure
    {.id = "pascal",     .aliases = {"pa"},          .symbol = "Pa",   .factor = 1e3,               .dimension = dimensions::PRESSURE, .family = families::SI, .prefixable = true},
    {.id = "bar",        .aliases = {},              .symbol = "bar",  .factor = 1e8,               .dimension = dimensions::PRESSURE, .family = families::SI, .prefixable = true},
    {.id = "atmosphere", .aliases = {"atm"},         .symbol = "atm",  .factor = 101325e3,          .dimension = dimensions::PRESSURE, .family = families::SI},
    {.id = "torr",       .aliases = {},              .symbol = "Torr", .factor = 101325e3 / 760,    .dimension = dimensions::PRESSURE, .family = families::SI},
    {.id = "mmhg",       .aliases = {},              .symbol = "mmHg", .factor = 101325e3 / 760,    .dimension = dimensions::PRESSURE, .family = families::SI},
    {.id = "inhg",       .aliases = {},              .symbol = "inHg", .factor = 3386.389e3,        .dimension = dimensions::PRESSURE, .family = families::IMPERIAL},
    {.id = "psi",        .aliases = {},              .symbol = "psi",  .factor = 6894.757293168e3,  .dimension = dimensions::PRESSURE, .family = families::IMPERIAL},
    {.id = "ksi",        .aliases = {},              .symbol = "ksi",  .factor = 6894757.293168e3,  .dimension = dimensions::PRESSURE, .family = families::IMPERIAL},
    // energy
    {.id = "joule",        .aliases = {"j"},            .symbol = "J",   .factor = 1e3,                  .dimension = dimensions::ENERGY, .family = families::SI, .prefixable = true},
    {.id = "calorie",      .aliases = {"cal"},          .symbol = "cal", .factor = 4.184e3,              .dimension = dimensions::ENERGY, .family = families::SI, .prefixable = true},
    {.id = "watthour",     .aliases = {"wh"},           .symbol = "Wh",  .name = "watt-hour", .factor = 3600e3,     .dimension = dimensions::ENERGY, .family = families::SI, .prefixable = true},
    {.id = "megawatthour", .aliases = {"mwh"},          .symbol = "MWh", .name = "megawatt-hour", .factor = 3600e9, .dimension = dimensions::ENERGY, .family = families::SI},
    {.id = "electronvolt", .aliases = {"ev"},           .symbol = "eV",  .factor = 1.602176634e-16,      .dimension = dimensions::ENERGY, .family = families::SI, .prefixable = true},
    {.id = "btu",          .aliases = {},               .symbol = "BTU", .factor = 1055.05585262e3,      .dimension = dimensions::ENERGY, .family = families::IMPERIAL},
    {.id = "erg",          .aliases = {},               .symbol = "erg", .factor = 1e-4,                 .dimension = dimensions::ENERGY, .family = families::SI},
    // power
    {.id = "watt",       .aliases = {"w"},   .symbol = "W",  .factor = 1e3,             .dimension = dimensions::POWER, .family = families::SI, .prefixable = true},
    {.id = "horsepower", .aliases = {"hp"},  .symbol = "hp", .factor = 745.69987158227e3, .dimension = dimensions::POWER, .family = families::IMPERIAL},
    // frequency
    {.id = "hertz", .aliases = {"hz"},  .symbol = "Hz",  .factor = 1,        .dimension = dimensions::FREQUENCY, .family = families::SI, .prefixable = true},
    {.id = "rpm",   .aliases = {},      .symbol = "rpm", .factor = 1.0 / 60, .dimension = dimensions::FREQUENCY, .family = families::SI},

    // currency (rates come from the currency provider; identity only). decimals
    // are the ISO 4217 / CLDR minor units; crypto gets satoshi precision.
    // a symbol is only stated where it reads back unambiguously: huf has none
    // because its "Ft" is the foot
    {.id = "usd", .aliases = {"dollar", "buck"},            .symbol = "$",   .name = "US Dollar",          .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "eur", .aliases = {"euro"},                      .symbol = "€",   .name = "Euro",               .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gbp", .aliases = {"pound", "quid", "sterling"}, .symbol = "£",   .name = "Pound Sterling",     .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "jpy", .aliases = {"yen"},                       .symbol = "¥",   .name = "Japanese Yen",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "cny", .aliases = {"yuan", "renminbi", "rmb"},   .symbol = "CN¥", .name = "Chinese Yuan",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "chf", .aliases = {"franc"},                                      .name = "Swiss Franc",                              .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "cad", .aliases = {},                            .symbol = "CA$", .name = "Canadian Dollar",    .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "aud", .aliases = {},                            .symbol = "A$",  .name = "Australian Dollar",  .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "nzd", .aliases = {},                            .symbol = "NZ$", .name = "New Zealand Dollar", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "hkd", .aliases = {},                            .symbol = "HK$", .name = "Hong Kong Dollar",   .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "sgd", .aliases = {},                            .symbol = "S$",  .name = "Singapore Dollar",   .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "twd", .aliases = {},                            .symbol = "NT$", .name = "New Taiwan Dollar",  .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "sek", .aliases = {"krona", "kronor"},                            .name = "Swedish Krona",                            .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "nok", .aliases = {"krone", "kroner"},                            .name = "Norwegian Krone",                          .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "dkk", .aliases = {},                                             .name = "Danish Krone",                             .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "pln", .aliases = {"zloty"},                     .symbol = "zł",  .name = "Polish Zloty",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "czk", .aliases = {"koruna"},                    .symbol = " Kč", .name = "Czech Koruna",                             .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},

	// huf instead of Ft to not conflict with "foot". We may need a better way to solve this.
    {.id = "huf", .aliases = {"forint"},                                     .name = "Hungarian Forint",                         .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},

    {.id = "ron", .aliases = {"leu", "lei"},                                 .name = "Romanian Leu",                             .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "try", .aliases = {"lira"},                      .symbol = "₺",   .name = "Turkish Lira",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "rub", .aliases = {"ruble", "rouble"},           .symbol = "₽",   .name = "Russian Ruble",      .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "uah", .aliases = {"hryvnia"},                   .symbol = "₴",   .name = "Ukrainian Hryvnia",  .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "inr", .aliases = {"rupee"},                     .symbol = "₹",   .name = "Indian Rupee",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "pkr", .aliases = {},                                             .name = "Pakistani Rupee",                          .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "krw", .aliases = {"won"},                       .symbol = "₩",   .name = "South Korean Won",   .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "thb", .aliases = {"baht"},                      .symbol = "฿",   .name = "Thai Baht",          .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "myr", .aliases = {"ringgit"},                   .symbol = "RM",  .name = "Malaysian Ringgit",  .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "idr", .aliases = {"rupiah"},                    .symbol = "Rp",  .name = "Indonesian Rupiah",  .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "php", .aliases = {},                            .symbol = "₱",   .name = "Philippine Peso",    .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "vnd", .aliases = {"dong"},                      .symbol = "₫",   .name = "Vietnamese Dong",    .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "aed", .aliases = {"dirham"},                                     .name = "United Arab Emirates Dirham",              .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "sar", .aliases = {"riyal"},                                      .name = "Saudi Riyal",                              .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ils", .aliases = {"shekel"},                    .symbol = "₪",   .name = "Israeli New Shekel", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "zar", .aliases = {"rand"},                      .symbol = "R",   .name = "South African Rand", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "egp", .aliases = {},                            .symbol = "E£",  .name = "Egyptian Pound",     .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ngn", .aliases = {"naira"},                     .symbol = "₦",   .name = "Nigerian Naira",     .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "brl", .aliases = {"real", "reais"},             .symbol = "R$",  .name = "Brazilian Real",     .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mxn", .aliases = {"peso"},                      .symbol = "MX$", .name = "Mexican Peso",       .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ars", .aliases = {},                                             .name = "Argentine Peso",                           .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "clp", .aliases = {},                                             .name = "Chilean Peso",                             .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "cop", .aliases = {},                                             .name = "Colombian Peso",                           .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "pen", .aliases = {"sol"},                                        .name = "Peruvian Sol",                             .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "isk", .aliases = {}, .name = "Icelandic Króna", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "bgn", .aliases = {"lev", "leva"}, .name = "Bulgarian Lev", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "hrk", .aliases = {"kuna"}, .name = "Croatian Kuna", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "rsd", .aliases = {}, .name = "Serbian Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bam", .aliases = {}, .name = "Bosnia-Herzegovina Convertible Mark", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mkd", .aliases = {"denar"}, .name = "Macedonian Denar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "all", .aliases = {"lek"}, .name = "Albanian Lek", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mdl", .aliases = {}, .name = "Moldovan Leu", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "byn", .aliases = {}, .name = "Belarusian Ruble", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gel", .aliases = {"lari"}, .symbol = "₾", .name = "Georgian Lari", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "amd", .aliases = {}, .symbol = "֏", .name = "Armenian Dram", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "azn", .aliases = {}, .symbol = "₼", .name = "Azerbaijani Manat", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kzt", .aliases = {"tenge"}, .symbol = "₸", .name = "Kazakhstani Tenge", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "uzs", .aliases = {"som"}, .name = "Uzbekistani Som", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kgs", .aliases = {}, .name = "Kyrgyzstani Som", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "tjs", .aliases = {"somoni"}, .name = "Tajikistani Somoni", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "tmt", .aliases = {}, .name = "Turkmenistani Manat", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mnt", .aliases = {"tugrik", "tögrög"}, .symbol = "₮", .name = "Mongolian Tugrik", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bdt", .aliases = {"taka"}, .symbol = "৳", .name = "Bangladeshi Taka", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "lkr", .aliases = {}, .name = "Sri Lankan Rupee", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "npr", .aliases = {}, .name = "Nepalese Rupee", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mvr", .aliases = {"rufiyaa"}, .name = "Maldivian Rufiyaa", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "btn", .aliases = {"ngultrum"}, .name = "Bhutanese Ngultrum", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "afn", .aliases = {"afghani"}, .symbol = "؋", .name = "Afghan Afghani", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "irr", .aliases = {}, .name = "Iranian Rial", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "iqd", .aliases = {}, .name = "Iraqi Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "kwd", .aliases = {}, .name = "Kuwaiti Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "bhd", .aliases = {}, .name = "Bahraini Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "omr", .aliases = {}, .name = "Omani Rial", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "qar", .aliases = {}, .name = "Qatari Riyal", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "jod", .aliases = {}, .name = "Jordanian Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "lbp", .aliases = {}, .name = "Lebanese Pound", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "syp", .aliases = {}, .name = "Syrian Pound", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "yer", .aliases = {}, .name = "Yemeni Rial", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mmk", .aliases = {"kyat"}, .name = "Myanmar Kyat", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "lak", .aliases = {"kip"}, .symbol = "₭", .name = "Laotian Kip", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "khr", .aliases = {"riel"}, .symbol = "៛", .name = "Cambodian Riel", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bnd", .aliases = {}, .name = "Brunei Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mop", .aliases = {"pataca"}, .name = "Macanese Pataca", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kpw", .aliases = {}, .name = "North Korean Won", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "pgk", .aliases = {"kina"}, .name = "Papua New Guinean Kina", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "fjd", .aliases = {}, .name = "Fijian Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "wst", .aliases = {"tala"}, .name = "Samoan Tala", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "top", .aliases = {"paanga"}, .name = "Tongan Paʻanga", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "vuv", .aliases = {"vatu"}, .name = "Vanuatu Vatu", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "sbd", .aliases = {}, .name = "Solomon Islands Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "xpf", .aliases = {}, .name = "CFP Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "mad", .aliases = {}, .name = "Moroccan Dirham", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "dzd", .aliases = {}, .name = "Algerian Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "tnd", .aliases = {}, .name = "Tunisian Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "lyd", .aliases = {}, .name = "Libyan Dinar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 3},
    {.id = "sdg", .aliases = {}, .name = "Sudanese Pound", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "etb", .aliases = {"birr"}, .name = "Ethiopian Birr", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ern", .aliases = {"nakfa"}, .name = "Eritrean Nakfa", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "djf", .aliases = {}, .name = "Djiboutian Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "sos", .aliases = {}, .name = "Somali Shilling", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kes", .aliases = {}, .name = "Kenyan Shilling", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ugx", .aliases = {}, .name = "Ugandan Shilling", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "tzs", .aliases = {}, .name = "Tanzanian Shilling", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "rwf", .aliases = {}, .name = "Rwandan Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "bif", .aliases = {}, .name = "Burundian Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "xaf", .aliases = {}, .name = "Central African CFA Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "xof", .aliases = {}, .name = "West African CFA Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "ghs", .aliases = {"cedi"}, .symbol = "₵", .name = "Ghanaian Cedi", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gmd", .aliases = {"dalasi"}, .name = "Gambian Dalasi", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gnf", .aliases = {}, .name = "Guinean Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "sle", .aliases = {"leone"}, .name = "Sierra Leonean Leone", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "lrd", .aliases = {}, .name = "Liberian Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "cve", .aliases = {"escudo"}, .name = "Cape Verdean Escudo", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mru", .aliases = {"ouguiya"}, .name = "Mauritanian Ouguiya", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "stn", .aliases = {"dobra"}, .name = "São Tomé and Príncipe Dobra", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "cdf", .aliases = {}, .name = "Congolese Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "aoa", .aliases = {"kwanza"}, .name = "Angolan Kwanza", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "zmw", .aliases = {}, .name = "Zambian Kwacha", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mwk", .aliases = {}, .name = "Malawian Kwacha", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mzn", .aliases = {"metical"}, .name = "Mozambican Metical", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mga", .aliases = {"ariary"}, .name = "Malagasy Ariary", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "mur", .aliases = {}, .name = "Mauritian Rupee", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "scr", .aliases = {}, .name = "Seychellois Rupee", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kmf", .aliases = {}, .name = "Comorian Franc", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "bwp", .aliases = {"pula"}, .name = "Botswanan Pula", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "nad", .aliases = {}, .name = "Namibian Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "szl", .aliases = {"lilangeni"}, .name = "Swazi Lilangeni", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "lsl", .aliases = {"loti"}, .name = "Lesotho Loti", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gtq", .aliases = {"quetzal"}, .name = "Guatemalan Quetzal", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "hnl", .aliases = {"lempira"}, .name = "Honduran Lempira", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "nio", .aliases = {"cordoba"}, .name = "Nicaraguan Córdoba", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "crc", .aliases = {}, .symbol = "₡", .name = "Costa Rican Colón", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "pab", .aliases = {"balboa"}, .name = "Panamanian Balboa", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "dop", .aliases = {}, .name = "Dominican Peso", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "cup", .aliases = {}, .name = "Cuban Peso", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "htg", .aliases = {"gourde"}, .name = "Haitian Gourde", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "jmd", .aliases = {}, .name = "Jamaican Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ttd", .aliases = {}, .name = "Trinidad and Tobago Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bbd", .aliases = {}, .name = "Barbadian Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bsd", .aliases = {}, .name = "Bahamian Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bzd", .aliases = {}, .name = "Belize Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bmd", .aliases = {}, .name = "Bermudan Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "kyd", .aliases = {}, .name = "Cayman Islands Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "xcd", .aliases = {}, .name = "East Caribbean Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "awg", .aliases = {"florin"}, .name = "Aruban Florin", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ang", .aliases = {"guilder"}, .name = "Netherlands Antillean Guilder", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "gyd", .aliases = {}, .name = "Guyanaese Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "srd", .aliases = {}, .name = "Surinamese Dollar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "bob", .aliases = {"boliviano"}, .name = "Bolivian Boliviano", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "pyg", .aliases = {"guarani"}, .symbol = "₲", .name = "Paraguayan Guarani", .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 0},
    {.id = "uyu", .aliases = {}, .name = "Uruguayan Peso", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "ves", .aliases = {"bolivar"}, .name = "Venezuelan Bolívar", .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 2},
    {.id = "btc", .aliases = {"bitcoin"},                   .symbol = "₿",   .name = "Bitcoin",            .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 8},
    {.id = "eth", .aliases = {"ether", "ethereum"},         .symbol = "Ξ",   .name = "Ethereum",           .symbolPrefix = true, .dimension = dimensions::CURRENCY, .family = families::CURRENCY, .decimals = 8},
  });
  // clang-format on

  return BUILTIN_UNITS;
}

// It is not needed to add compound aliases for e.g "km2" as this is automatically handled
// as part of the unit parsing. <unit>(2|3) is tolerated.
// clang-format off
std::span<const CompoundAlias> units::compoundAliases() {
  static constexpr auto ALIASES = std::to_array<CompoundAlias>({
      {.name = "mps",  .composition = "m/s"},
      {.name = "kmh",  .composition = "km/h"},
      {.name = "kph",  .composition = "km/h"},
      {.name = "mph",  .composition = "mi/h"},
      {.name = "sqm",  .composition = "m^2"},
      {.name = "sqkm", .composition = "km^2"},
      {.name = "sqft", .composition = "ft^2"},
      {.name = "sqmi", .composition = "mi^2"},
      {.name = "sqyd", .composition = "yd^2"},
      {.name = "sqin", .composition = "in^2"},
      {.name = "cc",   .composition = "cm^3"},
      {.name = "fps",  .composition = "ft/s"},
      {.name = "fpm",  .composition = "ft/min"},
      {.name = "kbps", .composition = "kbit/s"},
      {.name = "mbps", .composition = "Mbit/s"},
      {.name = "gbps", .composition = "Gbit/s"},
  });

  return ALIASES;
}
// clang-format on
