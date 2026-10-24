# CMake generated Testfile for 
# Source directory: /home/dev7z/CellScope-phreakShow
# Build directory: /home/dev7z/CellScope-phreakShow
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(map_projection_selftest "/home/dev7z/CellScope-phreakShow/map_projection_selftest")
set_tests_properties(map_projection_selftest PROPERTIES  _BACKTRACE_TRIPLES "/home/dev7z/CellScope-phreakShow/CMakeLists.txt;214;add_test;/home/dev7z/CellScope-phreakShow/CMakeLists.txt;0;")
add_test(antenna_database_selftest "/home/dev7z/CellScope-phreakShow/antenna_database_selftest")
set_tests_properties(antenna_database_selftest PROPERTIES  _BACKTRACE_TRIPLES "/home/dev7z/CellScope-phreakShow/CMakeLists.txt;214;add_test;/home/dev7z/CellScope-phreakShow/CMakeLists.txt;0;")
subdirs("third_party/lte")
