# Prefix all defined symbols in a static archive.
#
# Used by the Emscripten build to make the statically linked mod modules
# (cgame/ui/qagame) collision-free: each archive gets its own prefix
# (cg_ / ui_ / qa_) so identical aux sources (q_math.c, bg_*.c, ui_shared.c)
# compiled into several modules keep separate instances, exactly like the
# native shared-library layout.
#
# Invoked as:
#   cmake -DARCHIVE=<lib.a> -DPREFIX=cg_ -DNM=<llvm-nm> \
#         -DOBJCOPY=<llvm-objcopy> -DAR=<llvm-ar> \
#         -P cmake/prefix_symbols.cmake

if(NOT ARCHIVE OR NOT PREFIX OR NOT NM OR NOT OBJCOPY OR NOT AR)
	message(FATAL_ERROR "prefix_symbols: ARCHIVE, PREFIX, NM, OBJCOPY and AR are required")
endif()

get_filename_component(ARCHIVE_PATH "${ARCHIVE}" ABSOLUTE)
get_filename_component(ARCHIVE_DIR "${ARCHIVE_PATH}" DIRECTORY)
set(WORK_DIR "${ARCHIVE_DIR}/prefix_work")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# list member objects
execute_process(
	COMMAND ${AR} t "${ARCHIVE_PATH}"
	OUTPUT_VARIABLE OBJ_LIST
	OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE AR_LIST_RESULT
)
if(NOT AR_LIST_RESULT EQUAL 0)
	message(FATAL_ERROR "prefix_symbols: failed to list ${ARCHIVE_PATH}")
endif()

string(REPLACE "\n" ";" OBJECTS "${OBJ_LIST}")

# extract members
execute_process(
	COMMAND ${CMAKE_COMMAND} -E chdir "${WORK_DIR}" ${AR} x "${ARCHIVE_PATH}"
	RESULT_VARIABLE AR_X_RESULT
)
if(NOT AR_X_RESULT EQUAL 0)
	message(FATAL_ERROR "prefix_symbols: failed to extract ${ARCHIVE_PATH}")
endif()

set(REAL_OBJECTS "")
foreach(OBJ ${OBJECTS})
	if(NOT EXISTS "${WORK_DIR}/${OBJ}")
		continue()
	endif()
	list(APPEND REAL_OBJECTS "${OBJ}")

	execute_process(
		COMMAND ${NM} --defined-only "${WORK_DIR}/${OBJ}"
		OUTPUT_VARIABLE SYM_LINES
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	set(REDEF_CONTENT "")
	if(SYM_LINES)
		string(REPLACE "\n" ";" SYM_LIST "${SYM_LINES}")
		foreach(LINE ${SYM_LIST})
			string(REPLACE " " ";" PARTS "${LINE}")
			list(LENGTH PARTS NP)
			if(NP GREATER 2)
				list(GET PARTS 2 SYM)
				if(NOT SYM MATCHES "^${PREFIX}")
					string(APPEND REDEF_CONTENT "${SYM} ${PREFIX}${SYM}\n")
				endif()
			endif()
		endforeach()
	endif()

	if(REDEF_CONTENT)
		file(WRITE "${WORK_DIR}/redef.txt" "${REDEF_CONTENT}")
		execute_process(
			COMMAND ${OBJCOPY} --redefine-syms=${WORK_DIR}/redef.txt "${WORK_DIR}/${OBJ}" "${WORK_DIR}/${OBJ}"
			RESULT_VARIABLE OC_RESULT
		)
		if(NOT OC_RESULT EQUAL 0)
			message(FATAL_ERROR "prefix_symbols: objcopy failed for ${OBJ}")
		endif()
	endif()
endforeach()

# repack the archive
file(REMOVE "${ARCHIVE_PATH}")
execute_process(
	COMMAND ${CMAKE_COMMAND} -E chdir "${WORK_DIR}" ${AR} rcs "${ARCHIVE_PATH}" ${REAL_OBJECTS}
	RESULT_VARIABLE AR_R_RESULT
)
if(NOT AR_R_RESULT EQUAL 0)
	message(FATAL_ERROR "prefix_symbols: failed to repack ${ARCHIVE_PATH}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "prefix_symbols: ${PREFIX} applied to ${ARCHIVE_PATH}")
