##########################################################
# SQLite 3
##########################################################
file(GLOB DUNE_SQLITE3_FILES
  vendor/libraries/sqlite3/*.c)

if(DUNE_SYS_HAS_STDINT_H)
  set(SQLITE3_CXX_FLAGS "${SQLITE3_CXX_FLAGS} -DHAVE_STDINT_H")
endif(DUNE_SYS_HAS_STDINT_H)

if(DUNE_SYS_HAS_INTTYPES_H)
  set(SQLITE3_CXX_FLAGS "${SQLITE3_CXX_FLAGS} -DHAVE_INTTYPES_H")
endif(DUNE_SYS_HAS_INTTYPES_H)

if(DUNE_OS_RTEMS)
  set(SQLITE3_CXX_FLAGS "${SQLITE3_CXX_FLAGS} -DSQLITE_OMIT_WAL -DSQLITE_OMIT_LOAD_EXTENSION")
endif(DUNE_OS_RTEMS)

if(DUNE_OS_WINDOWS AND DUNE_CXX_GNU)
  set(SQLITE3_CXX_FLAGS "${SQLITE3_CXX_FLAGS} -Wno-sign-compare -Wno-uninitialized")
endif(DUNE_OS_WINDOWS AND DUNE_CXX_GNU)

set_source_files_properties(${DUNE_SQLITE3_FILES}
  PROPERTIES COMPILE_FLAGS "${DUNE_CXX_FLAGS} ${SQLITE3_CXX_FLAGS}")

list(APPEND DUNE_VENDOR_FILES ${DUNE_SQLITE3_FILES})
