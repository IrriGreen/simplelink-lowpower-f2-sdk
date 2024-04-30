# Copyright 2024, Irrigreen, Inc.  All rights reserved.

project('simplelink_lowpower_f2_sdk', 'c')

# The rest is taken from Ti's build of the sdk (binaries in the git repo).
#
# This list corresponds to what syscfg generates as
# ti_utils_build_linker.cmd.genlibs.  It may need to be kept in sync.
precompiled = [
    ['ti/drivers/lib/ticlang/m4f', 'drivers_cc26x2x7'],
    ['ti/grlib/lib/ticlang/m4f', 'grlib'],
]

compiler = meson.get_compiler('c')
simplelink_lowpower_f2_sdk_precompiled = []
foreach lib : precompiled
    simplelink_lowpower_f2_sdk_precompiled += compiler.find_library(
        lib[1],
        dirs: meson.current_source_dir() / 'source' / lib[0],
        static: true,
        required: true,
    )
endforeach

simplelink_lowpower_f2_sdk_inc = include_directories(
    'source',
    'source/ti/posix/ticlang',
    'kernel/freertos',
)



simplelink_lowpower_f2_sdk_dep = declare_dependency(
    dependencies: simplelink_lowpower_f2_sdk_precompiled,
    include_directories: simplelink_lowpower_f2_sdk_inc,
)

product_json = files('.metadata/product.json')
