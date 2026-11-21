cmake_minimum_required(VERSION 3.20)
project(LumavunPSNative LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# V0.5: self-authored PS-X EXE -> statically translated native Windows executable.
# Strictly limited MIPS-I demo subset, NOT a universal PS1 game port.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# V0.8.1: statically compile the original PPC64 homebrew ELF to C++ and native Windows.
# The test-only custom hostcall is not PS3 CellOS / syscall compatibility.
set(PPU_DEMO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PPU_DEMO_ELF "${PPU_DEMO_DIR}/PPU_HOMEBREW.ELF")
set(PPU_DEMO_CPP "${PPU_DEMO_DIR}/LumavunPPUHomebrewPort.cpp")
add_custom_command(
  OUTPUT "${PPU_DEMO_CPP}"
  BYPRODUCTS "${PPU_DEMO_ELF}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${PPU_DEMO_DIR}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_homebrew.py" "${PPU_DEMO_ELF}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_static_recomp.py" "${PPU_DEMO_ELF}" "${PPU_DEMO_CPP}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_homebrew.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_static_recomp.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_elf_layout.py"
  VERBATIM
)
# Exercise out-of-range and unaligned memory traps in *native MSVC executables*.
# Exact exit status is checked by Python so arbitrary crashes do not count as a pass.
foreach(variant IN ITEMS unmapped unaligned)
  if(variant STREQUAL "unmapped")
    set(PPU_TRAP_ADDRESS 0x1000)
  else()
    set(PPU_TRAP_ADDRESS 0x2002)
  endif()
  set(PPU_TRAP_CPP "${PPU_DEMO_DIR}/PPUGuestTrap_${variant}.cpp")
  set(PPU_TRAP_ELF "${PPU_DEMO_DIR}/PPUGuestTrap_${variant}.ELF")
  add_custom_command(
    OUTPUT "${PPU_TRAP_CPP}"
    BYPRODUCTS "${PPU_TRAP_ELF}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PPU_DEMO_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_homebrew.py"
            "${PPU_TRAP_ELF}" --guest-pointer "${PPU_TRAP_ADDRESS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_static_recomp.py"
            "${PPU_TRAP_ELF}" "${PPU_TRAP_CPP}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_homebrew.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ppu_static_recomp.py"
    VERBATIM
  )
  add_executable("LumavunPPUGuestTrap_${variant}" "${PPU_TRAP_CPP}")
  add_test(NAME "native_ppu_memory_guard_${variant}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_native_exit.py"
            12 "$<TARGET_FILE:LumavunPPUGuestTrap_${variant}>")
endforeach()
add_executable(LumavunPPUHomebrewPort "${PPU_DEMO_CPP}")
add_test(NAME native_ppu_homebrew COMMAND LumavunPPUHomebrewPort)
set_tests_properties(native_ppu_homebrew PROPERTIES PASS_REGULAR_EXPRESSION "PPU_HOMEBREW_RESULT=15")
add_test(NAME native_ppu_guest_big_endian COMMAND LumavunPPUHomebrewPort)
set_tests_properties(native_ppu_guest_big_endian PROPERTIES
  PASS_REGULAR_EXPRESSION "PPU_GUEST_BE_BYTES=18,52")
add_test(NAME native_ppu_nested_stack COMMAND LumavunPPUHomebrewPort)
set_tests_properties(native_ppu_nested_stack PROPERTIES
  PASS_REGULAR_EXPRESSION "PPU_STACK_RESTORED=1")
add_test(NAME native_ppu_elf_relative COMMAND LumavunPPUHomebrewPort)
set_tests_properties(native_ppu_elf_relative PROPERTIES
  PASS_REGULAR_EXPRESSION "PPU_ELF_RELOCATED=1")
add_test(NAME ppu_recomp_python_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_ppu_recomp.py" -v)
add_test(NAME ppu_memory_python_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_ppu_memory.py" -v)
add_test(NAME ppu_stack_python_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_ppu_stack.py" -v)
add_test(NAME ppu_elf_segment_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_ppu_elf_segments.py" -v)

set(PS1_DEMO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PS1_DEMO_INPUT "${PS1_DEMO_DIR}/PS1_MINIGAME.EXE")
set(PS1_DEMO_CPP "${PS1_DEMO_DIR}/LumavunPS1MiniPort.cpp")
add_custom_command(
  OUTPUT "${PS1_DEMO_CPP}"
  BYPRODUCTS "${PS1_DEMO_INPUT}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${PS1_DEMO_DIR}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_demo_ps1.py" "${PS1_DEMO_INPUT}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ps1_static_recomp.py" "${PS1_DEMO_INPUT}" "${PS1_DEMO_CPP}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_demo_ps1.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/ps1_static_recomp.py"
  VERBATIM
)

# Native Win32 windowed host for the SAME statically translated PS-X EXE.
# GDI / user32 graphics and event-driven keyboard input, not PlayStation hardware.
if(WIN32)
  set(PS1_GUI_CPP "${PS1_DEMO_DIR}/LumavunPS1MiniPortGui.cpp")
  add_custom_command(
    OUTPUT "${PS1_GUI_CPP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PS1_DEMO_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_demo_ps1.py" "${PS1_DEMO_INPUT}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ps1_static_recomp.py"
            "${PS1_DEMO_INPUT}" "${PS1_GUI_CPP}" --gui
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_demo_ps1.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ps1_static_recomp.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/native_game_win.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/native_runtime_state.hpp"
    VERBATIM
  )
  add_executable(LumavunPS1MiniPortGui WIN32 "${PS1_GUI_CPP}")
  target_include_directories(LumavunPS1MiniPortGui PRIVATE src)
  target_compile_definitions(LumavunPS1MiniPortGui PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(LumavunPS1MiniPortGui PRIVATE user32 gdi32)
  add_test(NAME native_ps1_gui_self_test COMMAND LumavunPS1MiniPortGui --self-test)
endif()
add_executable(native_runtime_tests tests/native_runtime_tests.cpp)
target_include_directories(native_runtime_tests PRIVATE src)
add_test(NAME native_runtime_tests COMMAND native_runtime_tests)

add_executable(LumavunPS1MiniPort "${PS1_DEMO_CPP}")
add_test(NAME native_ps1_minigame COMMAND LumavunPS1MiniPort --self-test)
set_tests_properties(native_ps1_minigame PROPERTIES
  PASS_REGULAR_EXPRESSION "RESULT score=2 turns=10")
add_test(NAME ps1_recomp_python_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_ps1_recomp.py" -v)

add_executable(preset_tests tests/preset_tests.cpp)
target_include_directories(preset_tests PRIVATE src)
add_test(NAME preset_tests COMMAND preset_tests)
add_executable(inspector_tests tests/inspector_tests.cpp)
target_include_directories(inspector_tests PRIVATE src)
add_test(NAME inspector_tests COMMAND inspector_tests)
add_executable(disc_tests tests/disc_tests.cpp)
target_include_directories(disc_tests PRIVATE src)
add_test(NAME disc_tests COMMAND disc_tests)
add_executable(ppu_inventory_tests tests/ppu_inventory_tests.cpp)
target_include_directories(ppu_inventory_tests PRIVATE src)
add_test(NAME ppu_inventory_tests COMMAND ppu_inventory_tests)
add_executable(lumavun_ppu_scan src/ppu_inventory_cli.cpp)
target_include_directories(lumavun_ppu_scan PRIVATE src)
add_test(NAME ppu_self_requires_elf
         COMMAND lumavun_ppu_scan "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv")
set_tests_properties(ppu_self_requires_elf PROPERTIES WILL_FAIL TRUE)
add_executable(feasibility_tests tests/feasibility_tests.cpp)
target_include_directories(feasibility_tests PRIVATE src)
add_test(NAME feasibility_tests COMMAND feasibility_tests)
add_executable(lumavun_probe src/console_probe_cli.cpp)
target_include_directories(lumavun_probe PRIVATE src)
add_test(NAME probe_cli_unrecognized
         COMMAND lumavun_probe "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv")
set_tests_properties(probe_cli_unrecognized PROPERTIES WILL_FAIL TRUE)
add_executable(pipeline_tests tests/pipeline_tests.cpp)
target_include_directories(pipeline_tests PRIVATE src)
add_test(NAME pipeline_tests COMMAND pipeline_tests)
add_executable(lumavun_port src/port_cli.cpp)
target_include_directories(lumavun_port PRIVATE src)
add_test(NAME port_backends COMMAND lumavun_port backends)
add_test(NAME port_compatibility COMMAND lumavun_port compatibility --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv")
add_test(NAME port_build_safely_blocked
         COMMAND lumavun_port build "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv"
                    --profile gundam-reborn --catalog "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv")
set_tests_properties(port_build_safely_blocked PROPERTIES WILL_FAIL TRUE)
add_custom_command(TARGET lumavun_port POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lumavun_port>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/data/compatibility.tsv"
      "$<TARGET_FILE_DIR:lumavun_port>/data/compatibility.tsv")

add_executable(lumavun_inspect src/inspect_cli.cpp)
target_include_directories(lumavun_inspect PRIVATE src)
if(WIN32)
  add_executable(LumavunPSLauncher WIN32 src/main_win.cpp)
  target_include_directories(LumavunPSLauncher PRIVATE src)
  target_compile_definitions(LumavunPSLauncher PRIVATE UNICODE _UNICODE)
  target_link_libraries(LumavunPSLauncher PRIVATE dxgi advapi32 user32 gdi32 shell32 comdlg32 ole32)
  if(MSVC)
    target_compile_options(LumavunPSLauncher PRIVATE /W4 /utf-8)
  endif()
endif()
