// Host-side tests for lib/ebus_core/src/homie/homie_datatype.cpp. Expected values come
// from the Homie 5 convention (https://homieiot.github.io/specification/), sections
// "Payloads" and "Formats", not from the implementation.

#include <unity.h>
#include <homie/homie_datatype.h>

void setUp(void) {}
void tearDown(void) {}

// --- integer payload syntax (Payloads/Integer) ----------------------------------

static void test_integer_payload_accepts_digits_and_leading_minus(void) {
    int64_t v = 0;
    TEST_ASSERT_TRUE(homie_parse_integer_payload("5", &v));
    TEST_ASSERT_EQUAL_INT64(5, v);
    TEST_ASSERT_TRUE(homie_parse_integer_payload("-42", &v));
    TEST_ASSERT_EQUAL_INT64(-42, v);
    TEST_ASSERT_TRUE(homie_parse_integer_payload("0", &v));
    TEST_ASSERT_EQUAL_INT64(0, v);
}

static void test_integer_payload_leading_plus_is_rejected(void) {
    int64_t v = 0;                       // '+' is not in the integer payload format
    TEST_ASSERT_FALSE(homie_parse_integer_payload("+5", &v));
}

static void test_integer_payload_with_fraction_is_rejected(void) {
    int64_t v = 0;                       // atoi() read this as 1
    TEST_ASSERT_FALSE(homie_parse_integer_payload("1.5", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("1.", &v));
}

static void test_integer_payload_with_exponent_is_rejected(void) {
    int64_t v = 0;                       // exponents are float-only
    TEST_ASSERT_FALSE(homie_parse_integer_payload("1e3", &v));
}

static void test_integer_payload_non_numeric_is_rejected(void) {
    int64_t v = 0;                       // atoi() read each of these as 0
    TEST_ASSERT_FALSE(homie_parse_integer_payload("abc", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("-", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("5abc", &v));
}

static void test_integer_payload_surrounding_space_is_rejected(void) {
    int64_t v = 0;                       // atoi() skipped leading whitespace
    TEST_ASSERT_FALSE(homie_parse_integer_payload(" 5", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("5 ", &v));
}

static void test_integer_payload_past_int32_is_accepted(void) {
    int64_t v = 0;                       // 32-bit storage used to refuse these
    TEST_ASSERT_TRUE(homie_parse_integer_payload("2147483648", &v));
    TEST_ASSERT_EQUAL_INT64(2147483648LL, v);
    TEST_ASSERT_TRUE(homie_parse_integer_payload("-2147483649", &v));
    TEST_ASSERT_EQUAL_INT64(-2147483649LL, v);
}

static void test_integer_payload_at_int64_bounds_is_accepted(void) {
    int64_t v = 0;                       // the spec's full integer width
    TEST_ASSERT_TRUE(homie_parse_integer_payload("9223372036854775807", &v));
    TEST_ASSERT_EQUAL_INT64(9223372036854775807LL, v);
    TEST_ASSERT_TRUE(homie_parse_integer_payload("-9223372036854775808", &v));
    TEST_ASSERT_EQUAL_INT64((-9223372036854775807LL - 1), v);
}

static void test_integer_payload_beyond_int64_is_rejected_not_saturated(void) {
    int64_t v = 0;                       // strtoll saturates and sets ERANGE; we refuse
    TEST_ASSERT_FALSE(homie_parse_integer_payload("9223372036854775808", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("-9223372036854775809", &v));
    TEST_ASSERT_FALSE(homie_parse_integer_payload("99999999999999999999", &v));
}

static void test_integer_payload_above_2_53_round_trips_exactly(void) {
    // The reason the /set path stores the parsed integer rather than the validator's
    // double when the format has no step: a double cannot hold these.
    int64_t v = 0;
    TEST_ASSERT_TRUE(homie_parse_integer_payload("9007199254740993", &v));   // 2^53 + 1
    TEST_ASSERT_EQUAL_INT64(9007199254740993LL, v);
    TEST_ASSERT_TRUE(homie_parse_integer_payload("-9007199254740993", &v));
    TEST_ASSERT_EQUAL_INT64(-9007199254740993LL, v);
}

// --- float payload syntax (Payloads/Float) --------------------------------------

static void test_float_payload_accepts_spec_spellings(void) {
    double v = 0;
    TEST_ASSERT_TRUE(homie_parse_float_payload("5", &v));
    TEST_ASSERT_EQUAL_DOUBLE(5.0, v);
    TEST_ASSERT_TRUE(homie_parse_float_payload("-1.5", &v));
    TEST_ASSERT_EQUAL_DOUBLE(-1.5, v);
    TEST_ASSERT_TRUE(homie_parse_float_payload("1e3", &v));
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, v);
    TEST_ASSERT_TRUE(homie_parse_float_payload("1.5E-3", &v));
    TEST_ASSERT_EQUAL_DOUBLE(0.0015, v);
}

static void test_float_payload_leading_plus_is_rejected(void) {
    double v = 0;
    TEST_ASSERT_FALSE(homie_parse_float_payload("+5", &v));
}

static void test_float_payload_non_numeric_is_rejected(void) {
    double v = 0;                        // atof() read each of these as 0.0
    TEST_ASSERT_FALSE(homie_parse_float_payload("abc", &v));
    TEST_ASSERT_FALSE(homie_parse_float_payload("", &v));
    TEST_ASSERT_FALSE(homie_parse_float_payload(".", &v));
}

static void test_float_payload_strtod_extensions_are_rejected(void) {
    double v = 0;                        // all three are things atof() would have taken
    TEST_ASSERT_FALSE(homie_parse_float_payload("0x10", &v));
    TEST_ASSERT_FALSE(homie_parse_float_payload("inf", &v));
    TEST_ASSERT_FALSE(homie_parse_float_payload("nan", &v));
}

static void test_float_payload_surrounding_space_is_rejected(void) {
    double v = 0;
    TEST_ASSERT_FALSE(homie_parse_float_payload(" 5", &v));
    TEST_ASSERT_FALSE(homie_parse_float_payload("5 ", &v));
}

static void test_float_payload_two_dots_is_rejected(void) {
    double v = 0;
    TEST_ASSERT_FALSE(homie_parse_float_payload("1.2.3", &v));
}

// --- number: no format ---------------------------------------------------------

static void test_number_without_format_accepts_any_value(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(-1e300, nullptr, &c));
    TEST_ASSERT_EQUAL_DOUBLE(-1e300, c);
    TEST_ASSERT_TRUE(homie_validate_number(42.5, "", &c));
    TEST_ASSERT_EQUAL_DOUBLE(42.5, c);
}

static void test_number_fully_open_format_accepts_any_value(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(-123456.0, ":", &c));
    TEST_ASSERT_EQUAL_DOUBLE(-123456.0, c);
}

static void test_number_coerced_pointer_may_be_null(void) {
    TEST_ASSERT_TRUE(homie_validate_number(5, "0:10", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(11, "0:10", nullptr));
}

// --- number: ranges --------------------------------------------------------------

static void test_number_range_minimum_is_inclusive(void) {
    TEST_ASSERT_TRUE(homie_validate_number(0, "0:10", nullptr));
}

static void test_number_range_maximum_is_inclusive(void) {
    TEST_ASSERT_TRUE(homie_validate_number(10, "0:10", nullptr));
}

static void test_number_just_below_minimum_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_number(-0.001, "0:10", nullptr));
}

static void test_number_just_above_maximum_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_number(10.001, "0:10", nullptr));
}

static void test_number_negative_minimum_from_spec_example(void) {
    TEST_ASSERT_TRUE(homie_validate_number(-20, "-20:120", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(-21, "-20:120", nullptr));
}

static void test_number_fractional_range_from_spec_example(void) {
    TEST_ASSERT_TRUE(homie_validate_number(12.5, "10.123:15.123", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(10.1, "10.123:15.123", nullptr));
}

static void test_number_missing_maximum_is_open_ended(void) {
    TEST_ASSERT_TRUE(homie_validate_number(1e12, "0:", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(-1, "0:", nullptr));
}

static void test_number_missing_minimum_is_open_ended(void) {
    TEST_ASSERT_TRUE(homie_validate_number(-1e12, ":10", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(11, ":10", nullptr));
}

static void test_number_large_integer_maximum_keeps_full_precision(void) {
    // Integer formats are 64-bit; 100000001 is not representable as a 32-bit float.
    TEST_ASSERT_TRUE(homie_validate_number(100000001, "0:100000001", nullptr));
}

static void test_number_format_accepts_exponent_notation(void) {
    TEST_ASSERT_TRUE(homie_validate_number(500, "0:1e3", nullptr));
}

// --- number: step ----------------------------------------------------------------

static void test_number_value_on_step_is_unchanged(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(4, "2:6:2", &c));
    TEST_ASSERT_EQUAL_DOUBLE(4, c);
}

static void test_number_step_example_from_spec_rounds_with_min_base(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(5, "0:10:2", &c));
    TEST_ASSERT_EQUAL_DOUBLE(6, c);
}

static void test_number_step_example_from_spec_rounds_half_up_with_max_base(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(5, ":10:2", &c));
    TEST_ASSERT_EQUAL_DOUBLE(6, c);
}

static void test_number_step_rounds_to_nearest_step(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(5.9, "0:10:4", &c));
    TEST_ASSERT_EQUAL_DOUBLE(4, c);
    TEST_ASSERT_TRUE(homie_validate_number(6.1, "0:10:4", &c));
    TEST_ASSERT_EQUAL_DOUBLE(8, c);
}

static void test_number_range_check_happens_after_rounding_up(void) {
    // 11 rounds to 12 on a 0-based step of 4, which is above max.
    double c = 0;
    TEST_ASSERT_FALSE(homie_validate_number(11, "0:10:4", &c));
    TEST_ASSERT_EQUAL_DOUBLE(12, c);
}

static void test_number_out_of_range_value_that_rounds_into_range_is_accepted(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(10.4, "0:10:1", &c));
    TEST_ASSERT_EQUAL_DOUBLE(10, c);
}

static void test_number_step_uses_min_as_base_not_zero(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(2, "1:9:3", &c));
    TEST_ASSERT_EQUAL_DOUBLE(1, c);
    TEST_ASSERT_TRUE(homie_validate_number(3, "1:9:3", &c));
    TEST_ASSERT_EQUAL_DOUBLE(4, c);
}

static void test_number_fractional_step_rounds(void) {
    double c = 0;
    TEST_ASSERT_TRUE(homie_validate_number(0.3, "0:1:0.25", &c));
    TEST_ASSERT_EQUAL_DOUBLE(0.25, c);
}

// --- number: malformed format ------------------------------------------------------

static void test_number_format_without_colon_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "10", nullptr));
}

static void test_number_format_with_non_numeric_field_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "a:10", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:ten", nullptr));
}

static void test_number_format_field_with_plus_sign_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "+0:10", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:1e+3", nullptr));
}

static void test_number_format_negative_exponent_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_number(0.0015, "1E-3:2e-3", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(0.0025, "1E-3:2e-3", nullptr));
}

static void test_number_format_incomplete_or_overflowing_exponent_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:1e", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:1e-", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:e3", nullptr));
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:1e999", nullptr));
}

static void test_number_zero_step_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:10:0", nullptr));
}

static void test_number_negative_step_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:10:-2", nullptr));
}

static void test_number_format_with_extra_field_is_malformed(void) {
    TEST_ASSERT_FALSE(homie_validate_number(5, "0:10:2:4", nullptr));
}

static void test_number_malformed_format_leaves_value_uncoerced(void) {
    double c = 0;
    TEST_ASSERT_FALSE(homie_validate_number(5, "x", &c));
    TEST_ASSERT_EQUAL_DOUBLE(5, c);
}

static void test_number_format_parse_reports_open_ends(void) {
    HomieNumberFormat nf;
    TEST_ASSERT_TRUE(homie_parse_number_format(":10:2", &nf));
    TEST_ASSERT_FALSE(nf.has_min);
    TEST_ASSERT_TRUE(nf.has_max);
    TEST_ASSERT_TRUE(nf.has_step);
    TEST_ASSERT_EQUAL_DOUBLE(10, nf.max);
    TEST_ASSERT_EQUAL_DOUBLE(2, nf.step);
}

// --- enum ------------------------------------------------------------------------

static void test_enum_listed_value_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_enum("open", "close,open,stop"));
    TEST_ASSERT_TRUE(homie_validate_enum("close", "close,open,stop"));
    TEST_ASSERT_TRUE(homie_validate_enum("stop", "close,open,stop"));
}

static void test_enum_single_value_format_accepts_that_value(void) {
    TEST_ASSERT_TRUE(homie_validate_enum("only", "only"));
}

static void test_enum_unlisted_value_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("half", "close,open,stop"));
}

static void test_enum_is_case_sensitive(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("Car", "car,bike"));
}

static void test_enum_leading_whitespace_is_significant(void) {
    TEST_ASSERT_FALSE(homie_validate_enum(" Car", "Car,Bike"));
    TEST_ASSERT_TRUE(homie_validate_enum(" Bike", "Car, Bike"));
    TEST_ASSERT_FALSE(homie_validate_enum("Bike", "Car, Bike"));
}

static void test_enum_trailing_whitespace_is_significant(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("Car ", "Car,Bike"));
}

static void test_enum_prefix_of_listed_value_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("op", "close,open"));
    TEST_ASSERT_FALSE(homie_validate_enum("opened", "close,open"));
}

static void test_enum_payload_spanning_two_values_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("close,open", "close,open"));
}

static void test_enum_empty_payload_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("", "a,b"));
    TEST_ASSERT_FALSE(homie_validate_enum(nullptr, "a,b"));
}

static void test_enum_missing_format_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_enum("a", ""));
    TEST_ASSERT_FALSE(homie_validate_enum("a", nullptr));
}

// --- color -----------------------------------------------------------------------

static void test_color_rgb_example_from_spec_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_color("rgb,100,100,100", "rgb"));
}

static void test_color_hsv_example_from_spec_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_color("hsv,300,50,75", "hsv"));
}

static void test_color_xyz_example_from_spec_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_color("xyz,0.25,0.34", "xyz"));
}

static void test_color_rgb_range_edges_are_inclusive(void) {
    TEST_ASSERT_TRUE(homie_validate_color("rgb,0,0,0", "rgb"));
    TEST_ASSERT_TRUE(homie_validate_color("rgb,255,255,255", "rgb"));
}

static void test_color_rgb_out_of_range_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,256,0,0", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,0,255.5,0", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,0,0,-1", "rgb"));
}

static void test_color_hsv_range_edges_are_inclusive(void) {
    TEST_ASSERT_TRUE(homie_validate_color("hsv,0,0,0", "hsv"));
    TEST_ASSERT_TRUE(homie_validate_color("hsv,360,100,100", "hsv"));
}

static void test_color_hsv_out_of_range_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("hsv,360.1,50,50", "hsv"));
    TEST_ASSERT_FALSE(homie_validate_color("hsv,180,101,50", "hsv"));
    TEST_ASSERT_FALSE(homie_validate_color("hsv,180,50,101", "hsv"));
}

static void test_color_xyz_range_edges_are_inclusive(void) {
    TEST_ASSERT_TRUE(homie_validate_color("xyz,0,0", "xyz"));
    TEST_ASSERT_TRUE(homie_validate_color("xyz,1,1", "xyz"));
}

static void test_color_xyz_out_of_range_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("xyz,1.01,0", "xyz"));
    TEST_ASSERT_FALSE(homie_validate_color("xyz,0,-0.01", "xyz"));
}

static void test_color_type_not_in_format_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,2,3", "hsv"));
    TEST_ASSERT_FALSE(homie_validate_color("xyz,0.1,0.2", "rgb,hsv"));
}

static void test_color_type_listed_anywhere_in_format_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_color("hsv,1,2,3", "rgb,hsv"));
    TEST_ASSERT_TRUE(homie_validate_color("rgb,1,2,3", "rgb,hsv"));
}

static void test_color_unknown_type_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("cmy,1,2,3", "cmy"));
}

static void test_color_uppercase_type_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("RGB,1,2,3", "rgb"));
}

static void test_color_too_few_components_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,2", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("xyz,0.1", "xyz"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb", "rgb"));
}

static void test_color_too_many_components_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,2,3,4", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("xyz,0.1,0.2,0.7", "xyz"));
}

static void test_color_trailing_comma_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,2,3,", "rgb"));
}

static void test_color_empty_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,,3", "rgb"));
}

static void test_color_spaces_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb, 1,2,3", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1,2,3 ", "rgb"));
}

static void test_color_non_numeric_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,a,2,3", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,1.2.3,2,3", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,NaN,2,3", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color("rgb,-,2,3", "rgb"));
}

static void test_color_empty_payload_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("", "rgb"));
    TEST_ASSERT_FALSE(homie_validate_color(nullptr, "rgb"));
}

static void test_color_component_with_exponent_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_color("rgb,1e2,0,0", "rgb"));
}

static void test_color_component_with_plus_sign_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_color("rgb,+1,0,0", "rgb"));
}

static void test_color_parse_returns_type_and_components(void) {
    HomieColor col;
    TEST_ASSERT_TRUE(homie_parse_color("hsv,300,50,75", "hsv", &col));
    TEST_ASSERT_EQUAL_INT(1, col.type);
    TEST_ASSERT_EQUAL_FLOAT(300, col.c[0]);
    TEST_ASSERT_EQUAL_FLOAT(50, col.c[1]);
    TEST_ASSERT_EQUAL_FLOAT(75, col.c[2]);
}

static void test_color_parse_xyz_leaves_third_component_zero(void) {
    HomieColor col;
    TEST_ASSERT_TRUE(homie_parse_color("xyz,0.25,0.34", nullptr, &col));
    TEST_ASSERT_EQUAL_INT(2, col.type);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, col.c[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.34f, col.c[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, col.c[2]);
}

static void test_color_parse_without_format_skips_membership_check(void) {
    HomieColor col;
    TEST_ASSERT_TRUE(homie_parse_color("rgb,1,2,3", "", &col));
    TEST_ASSERT_EQUAL_INT(0, col.type);
}

// --- datetime --------------------------------------------------------------------

static void test_datetime_utc_timestamp_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00Z"));
}

static void test_datetime_with_fractional_seconds_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00.123Z"));
}

static void test_datetime_with_offset_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00+05:30"));
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00-08:00"));
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00-08"));
}

static void test_datetime_without_zone_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30:00"));
}

static void test_datetime_without_seconds_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15T10:30"));
}

static void test_datetime_date_only_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-01-15"));
}

static void test_datetime_leap_second_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2016-12-31T23:59:60Z"));
}

static void test_datetime_empty_payload_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime(""));
    TEST_ASSERT_FALSE(homie_validate_datetime(nullptr));
}

static void test_datetime_month_out_of_range_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-13-01"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-00-01"));
}

static void test_datetime_day_out_of_range_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-32"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-00"));
}

static void test_datetime_day_past_end_of_month_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2023-02-29"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-04-31"));
}

static void test_datetime_leap_day_follows_gregorian_rule(void) {
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-02-29"));
    TEST_ASSERT_TRUE(homie_validate_datetime("2000-02-29"));
    TEST_ASSERT_FALSE(homie_validate_datetime("1900-02-29"));
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-04-30"));
    TEST_ASSERT_TRUE(homie_validate_datetime("2024-12-31"));
}

static void test_datetime_hour_out_of_range_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T25:00:00Z"));
}

static void test_datetime_minute_out_of_range_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:60:00Z"));
}

static void test_datetime_second_out_of_range_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:30:61Z"));
}

static void test_datetime_truncated_date_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024"));
}

static void test_datetime_unpadded_fields_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-1-15"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T1:30"));
}

static void test_datetime_t_without_time_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T"));
}

static void test_datetime_trailing_characters_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:30:00Zjunk"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15x"));
}

static void test_datetime_malformed_offset_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:30:00+5"));
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:30:00+05:3"));
}

static void test_datetime_fraction_without_digits_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("2024-01-15T10:30:00.Z"));
}

static void test_datetime_non_date_text_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_datetime("yesterday"));
    TEST_ASSERT_FALSE(homie_validate_datetime("1705314600"));
}

// --- duration --------------------------------------------------------------------

static void test_duration_examples_from_spec_are_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_duration("PT12H5M46S"));
    TEST_ASSERT_TRUE(homie_validate_duration("PT5M"));
}

static void test_duration_single_hour_or_second_component_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_duration("PT1H"));
    TEST_ASSERT_TRUE(homie_validate_duration("PT30S"));
}

static void test_duration_fractional_seconds_are_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_duration("PT1.5S"));
}

static void test_duration_zero_is_accepted(void) {
    TEST_ASSERT_TRUE(homie_validate_duration("PT0S"));
}

static void test_duration_empty_payload_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration(""));
    TEST_ASSERT_FALSE(homie_validate_duration(nullptr));
}

static void test_duration_without_leading_p_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("T5M"));
    TEST_ASSERT_FALSE(homie_validate_duration("5M"));
}

static void test_duration_lowercase_designators_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("pt5m"));
}

static void test_duration_with_no_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("P"));
    TEST_ASSERT_FALSE(homie_validate_duration("PT"));
}

static void test_duration_number_without_unit_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT5"));
}

static void test_duration_unit_without_number_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PTH"));
}

static void test_duration_unknown_time_unit_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT5X"));
    TEST_ASSERT_FALSE(homie_validate_duration("PT5D"));
}

static void test_duration_negative_number_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT-5S"));
}

static void test_duration_second_t_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT1HT5M"));
}

static void test_duration_fraction_without_digits_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT1.S"));
}

static void test_duration_spaces_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT 5M"));
    TEST_ASSERT_FALSE(homie_validate_duration("PT5M "));
}

static void test_duration_without_time_designator_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("P3D"));
    TEST_ASSERT_FALSE(homie_validate_duration("P1DT2H"));
}

static void test_duration_components_out_of_order_are_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT5S5H"));
}

static void test_duration_repeated_component_is_rejected(void) {
    TEST_ASSERT_FALSE(homie_validate_duration("PT1H2H"));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_integer_payload_accepts_digits_and_leading_minus);
    RUN_TEST(test_integer_payload_leading_plus_is_rejected);
    RUN_TEST(test_integer_payload_with_fraction_is_rejected);
    RUN_TEST(test_integer_payload_with_exponent_is_rejected);
    RUN_TEST(test_integer_payload_non_numeric_is_rejected);
    RUN_TEST(test_integer_payload_surrounding_space_is_rejected);
    RUN_TEST(test_integer_payload_past_int32_is_accepted);
    RUN_TEST(test_integer_payload_at_int64_bounds_is_accepted);
    RUN_TEST(test_integer_payload_beyond_int64_is_rejected_not_saturated);
    RUN_TEST(test_integer_payload_above_2_53_round_trips_exactly);

    RUN_TEST(test_float_payload_accepts_spec_spellings);
    RUN_TEST(test_float_payload_leading_plus_is_rejected);
    RUN_TEST(test_float_payload_non_numeric_is_rejected);
    RUN_TEST(test_float_payload_strtod_extensions_are_rejected);
    RUN_TEST(test_float_payload_surrounding_space_is_rejected);
    RUN_TEST(test_float_payload_two_dots_is_rejected);

    RUN_TEST(test_number_without_format_accepts_any_value);
    RUN_TEST(test_number_fully_open_format_accepts_any_value);
    RUN_TEST(test_number_coerced_pointer_may_be_null);
    RUN_TEST(test_number_range_minimum_is_inclusive);
    RUN_TEST(test_number_range_maximum_is_inclusive);
    RUN_TEST(test_number_just_below_minimum_is_rejected);
    RUN_TEST(test_number_just_above_maximum_is_rejected);
    RUN_TEST(test_number_negative_minimum_from_spec_example);
    RUN_TEST(test_number_fractional_range_from_spec_example);
    RUN_TEST(test_number_missing_maximum_is_open_ended);
    RUN_TEST(test_number_missing_minimum_is_open_ended);
    RUN_TEST(test_number_large_integer_maximum_keeps_full_precision);
    RUN_TEST(test_number_format_accepts_exponent_notation);
    RUN_TEST(test_number_value_on_step_is_unchanged);
    RUN_TEST(test_number_step_example_from_spec_rounds_with_min_base);
    RUN_TEST(test_number_step_example_from_spec_rounds_half_up_with_max_base);
    RUN_TEST(test_number_step_rounds_to_nearest_step);
    RUN_TEST(test_number_range_check_happens_after_rounding_up);
    RUN_TEST(test_number_out_of_range_value_that_rounds_into_range_is_accepted);
    RUN_TEST(test_number_step_uses_min_as_base_not_zero);
    RUN_TEST(test_number_fractional_step_rounds);
    RUN_TEST(test_number_format_without_colon_is_malformed);
    RUN_TEST(test_number_format_with_non_numeric_field_is_malformed);
    RUN_TEST(test_number_format_field_with_plus_sign_is_malformed);
    RUN_TEST(test_number_format_negative_exponent_is_accepted);
    RUN_TEST(test_number_format_incomplete_or_overflowing_exponent_is_malformed);
    RUN_TEST(test_number_zero_step_is_malformed);
    RUN_TEST(test_number_negative_step_is_malformed);
    RUN_TEST(test_number_format_with_extra_field_is_malformed);
    RUN_TEST(test_number_malformed_format_leaves_value_uncoerced);
    RUN_TEST(test_number_format_parse_reports_open_ends);

    RUN_TEST(test_enum_listed_value_is_accepted);
    RUN_TEST(test_enum_single_value_format_accepts_that_value);
    RUN_TEST(test_enum_unlisted_value_is_rejected);
    RUN_TEST(test_enum_is_case_sensitive);
    RUN_TEST(test_enum_leading_whitespace_is_significant);
    RUN_TEST(test_enum_trailing_whitespace_is_significant);
    RUN_TEST(test_enum_prefix_of_listed_value_is_rejected);
    RUN_TEST(test_enum_payload_spanning_two_values_is_rejected);
    RUN_TEST(test_enum_empty_payload_is_rejected);
    RUN_TEST(test_enum_missing_format_is_rejected);

    RUN_TEST(test_color_rgb_example_from_spec_is_accepted);
    RUN_TEST(test_color_hsv_example_from_spec_is_accepted);
    RUN_TEST(test_color_xyz_example_from_spec_is_accepted);
    RUN_TEST(test_color_rgb_range_edges_are_inclusive);
    RUN_TEST(test_color_rgb_out_of_range_component_is_rejected);
    RUN_TEST(test_color_hsv_range_edges_are_inclusive);
    RUN_TEST(test_color_hsv_out_of_range_component_is_rejected);
    RUN_TEST(test_color_xyz_range_edges_are_inclusive);
    RUN_TEST(test_color_xyz_out_of_range_component_is_rejected);
    RUN_TEST(test_color_type_not_in_format_is_rejected);
    RUN_TEST(test_color_type_listed_anywhere_in_format_is_accepted);
    RUN_TEST(test_color_unknown_type_is_rejected);
    RUN_TEST(test_color_uppercase_type_is_rejected);
    RUN_TEST(test_color_too_few_components_is_rejected);
    RUN_TEST(test_color_too_many_components_is_rejected);
    RUN_TEST(test_color_trailing_comma_is_rejected);
    RUN_TEST(test_color_empty_component_is_rejected);
    RUN_TEST(test_color_spaces_are_rejected);
    RUN_TEST(test_color_non_numeric_component_is_rejected);
    RUN_TEST(test_color_empty_payload_is_rejected);
    RUN_TEST(test_color_component_with_exponent_is_accepted);
    RUN_TEST(test_color_component_with_plus_sign_is_rejected);
    RUN_TEST(test_color_parse_returns_type_and_components);
    RUN_TEST(test_color_parse_xyz_leaves_third_component_zero);
    RUN_TEST(test_color_parse_without_format_skips_membership_check);

    RUN_TEST(test_datetime_utc_timestamp_is_accepted);
    RUN_TEST(test_datetime_with_fractional_seconds_is_accepted);
    RUN_TEST(test_datetime_with_offset_is_accepted);
    RUN_TEST(test_datetime_without_zone_is_accepted);
    RUN_TEST(test_datetime_without_seconds_is_accepted);
    RUN_TEST(test_datetime_date_only_is_accepted);
    RUN_TEST(test_datetime_leap_second_is_accepted);
    RUN_TEST(test_datetime_empty_payload_is_rejected);
    RUN_TEST(test_datetime_month_out_of_range_is_rejected);
    RUN_TEST(test_datetime_day_out_of_range_is_rejected);
    RUN_TEST(test_datetime_day_past_end_of_month_is_rejected);
    RUN_TEST(test_datetime_leap_day_follows_gregorian_rule);
    RUN_TEST(test_datetime_hour_out_of_range_is_rejected);
    RUN_TEST(test_datetime_minute_out_of_range_is_rejected);
    RUN_TEST(test_datetime_second_out_of_range_is_rejected);
    RUN_TEST(test_datetime_truncated_date_is_rejected);
    RUN_TEST(test_datetime_unpadded_fields_are_rejected);
    RUN_TEST(test_datetime_t_without_time_is_rejected);
    RUN_TEST(test_datetime_trailing_characters_are_rejected);
    RUN_TEST(test_datetime_malformed_offset_is_rejected);
    RUN_TEST(test_datetime_fraction_without_digits_is_rejected);
    RUN_TEST(test_datetime_non_date_text_is_rejected);

    RUN_TEST(test_duration_examples_from_spec_are_accepted);
    RUN_TEST(test_duration_single_hour_or_second_component_is_accepted);
    RUN_TEST(test_duration_fractional_seconds_are_accepted);
    RUN_TEST(test_duration_zero_is_accepted);
    RUN_TEST(test_duration_empty_payload_is_rejected);
    RUN_TEST(test_duration_without_leading_p_is_rejected);
    RUN_TEST(test_duration_lowercase_designators_are_rejected);
    RUN_TEST(test_duration_with_no_component_is_rejected);
    RUN_TEST(test_duration_number_without_unit_is_rejected);
    RUN_TEST(test_duration_unit_without_number_is_rejected);
    RUN_TEST(test_duration_unknown_time_unit_is_rejected);
    RUN_TEST(test_duration_negative_number_is_rejected);
    RUN_TEST(test_duration_second_t_is_rejected);
    RUN_TEST(test_duration_fraction_without_digits_is_rejected);
    RUN_TEST(test_duration_spaces_are_rejected);
    RUN_TEST(test_duration_without_time_designator_is_rejected);
    RUN_TEST(test_duration_components_out_of_order_are_rejected);
    RUN_TEST(test_duration_repeated_component_is_rejected);

    return UNITY_END();
}
