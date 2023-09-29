export plugin_path="$(pwd)/build/libEstimateCostPass.so"
export deps_path=(
    "$(pwd)/../WuLarus/A1.Branch_prediction/build/libBranchPredictionPass.so"
    "$(pwd)/../WuLarus/A2.Block_edge_frequency/build/libBlockEdgeFrequencyPass.so"
    "$(pwd)/../WuLarus/A3.Function_call_frequency/build/libFunctionCallFrequencyPass.so"
)
