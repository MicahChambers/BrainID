# code/utils/vul/CMakeLists.txt

INCLUDE_DIRECTORIES(
    ${brainid_SOURCE_DIR}/code/utils/vul
    )

SET(vul_sources
  vul_printf.h vul_printf.cxx
  vul_reg_exp.h vul_reg_exp.cxx
  vul_sprintf.h vul_sprintf.cxx
  vul_string.h vul_string.cxx
  vul_arg.h vul_arg.cxx
)

ADD_LIBRARY( vullib ${vul_sources} )
TARGET_LINK_LIBRARIES( vullib itkvcl )
