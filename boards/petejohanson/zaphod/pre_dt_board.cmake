# Suppresses duplicate unit-address warning at build time for power, clock, acl and flash-controller
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
