##############################################################################
#
# Library:   TubeTK
#
# Copyright Kitware Inc.
#
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
##############################################################################

include_regular_expression( "^.*$" )

set( PROJ_EXE ${TubeTK_LAUNCHER} $<TARGET_FILE:${MODULE_NAME}> )

itk_add_test(
  NAME ${MODULE_NAME}-Test1
  COMMAND ${PROJ_EXE}
    --parameterProgression ${ITK_TEST_OUTPUT_DIR}/${MODULE_NAME}Test1ParameterProgression.h5
    DATA{${TubeTK_DATA_ROOT}/Branch.n020.mha}
    DATA{${TubeTK_DATA_ROOT}/tube.tre}
    ${ITK_TEST_OUTPUT_DIR}/${MODULE_NAME}Test1.h5 )

set( METRIC_SAMPLER ComputeImageToTubeRigidMetricImage )
set( METRIC_SAMPLER_EXE ${TubeTK_LAUNCHER} $<TARGET_FILE:${METRIC_SAMPLER}> )

itk_add_test(
  NAME ${MODULE_NAME}-MetricSampler-Test1
  COMMAND ${METRIC_SAMPLER_EXE}
    DATA{${TubeTK_DATA_ROOT}/Branch.n020.mha}
    DATA{${TubeTK_DATA_ROOT}/tube.tre}
    ${ITK_TEST_OUTPUT_DIR}/${MODULE_NAME}-MetricSampler-Test1.nrrd )

set( METRIC_EXTRACTOR ExtractMetricImageSlice )
set( METRIC_EXTRACTOR_EXE ${TubeTK_LAUNCHER} $<TARGET_FILE:${METRIC_EXTRACTOR}> )

itk_add_test(
  NAME ${MODULE_NAME}-MetricExtractor-Test1
  COMMAND ${METRIC_EXTRACTOR_EXE}
    ${ITK_TEST_OUTPUT_DIR}/${MODULE_NAME}-MetricSampler-Test1.nrrd
    ${ITK_TEST_OUTPUT_DIR}/${MODULE_NAME}-MetricExtractor-Test1.mha )
set_tests_properties( ${MODULE_NAME}-MetricExtractor-Test1
  PROPERTIES DEPENDS ${MODULE_NAME}-MetricSampler-Test1 )
