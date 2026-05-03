// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <pybind11/numpy.h>
#include <pybind11/stl_bind.h>
#include <vector>
#include "netlist/DesignObject.hh"
#include "netlist/Pin.hh"
#include "netlist/Netlist.hh"
#include "timing_api.hh"
#include "sta/StaDelayPropagation.hh"
#include "sta/StaSlewPropagation.hh"
#include "Lib.hh"
#include <set>
#include <string>
#include <vector>


namespace python_interface {

struct WireTimingData
{
  std::string _from_node_name;
  std::string _to_node_name;
  double _wire_resistance;
  double _wire_capacitance;
  double _wire_from_slew;
  double _wire_to_slew;
  double _wire_delay;
};

using PathWireTimingData = std::vector<WireTimingData>;

bool staRun(const std::string& output);

bool staInit(const std::string& output);

bool staReport(const std::string& output);
bool setDesignWorkSpace(const std::string& design_workspace);

bool initLog(std::string log_path);

bool read_lef_def(std::vector<std::string>& lef_files, const std::string& def_file);
bool readVerilog(const std::string& file_name);

bool readLiberty(std::vector<std::string>& lib_files);

bool linkDesign(const std::string& cell_name);

bool readSpef(const std::string& file_name);

bool readSdc(const std::string& file_name);

std::string getNetName(const std::string& pin_port_name);

double getSegmentResistance(int layer_id, double segment_length, int route_layer_id);
double getSegmentCapacitance(int layer_id, double segment_length, int route_layer_id);

std::string makeRCTreeInnerNode(const std::string& net_name, int id, float cap);
std::string makeRCTreeObjNode(const std::string& pin_port_name, float cap);
bool makeRCTreeEdge(const std::string& net_name, std::string& node1, std::string& node2, float res);
bool updateRCTreeInfo(const std::string& net_name);
bool updateTiming();
bool buildTimingRcTree(const std::string& routing_type);
bool writeTimingModel(const std::string& output_lib_path,
                      const std::string& analysis_mode = "max");
bool reportSta();

std::vector<PathWireTimingData> getWireTimingData(unsigned n_worst_path_per_clock);

void build_timing_graph();
void update_clock_timing();
void buildRcTreeFromFlatData(
    const std::string& netName,
    const std::vector<std::string>& node_sta_names,
    const std::vector<bool>& node_is_pin, 
    const std::vector<int>& steiner_indices,
    const std::vector<int>& parent_indices,
    const std::vector<double>& node_total_caps,
    const std::vector<double>& edge_resistances,
    const std::vector<int>& node_global_indices);
void collectRctDataAndFillList(
    ista::RctNode* node,
    ista::Net* net,
    pybind11::list& results_list,
    std::unordered_set<ista::RctNode*>& visited);
void updateAndGetAllPinTimings(
  const std::vector<std::string>& pin_names,
  pybind11::list& arrival_late_times,
  pybind11::list& arrival_early_times,
  pybind11::list& required_late_times,
  pybind11::list& required_early_times,
  pybind11::list& pin_net_delay,
  pybind11::list& cell_arc_delays,
  pybind11::list& net_timing_details // 结果将直接填充到这里
);
void convertDBToTimingNetlist();
bool reportTiming(int digits, 
  const std::string& delay_type, 
  std::vector<std::string> exclude_cell_names, 
  bool derate, 
  bool is_clock_cap, 
  bool is_not_bak_rpt, 
  int max_path, 
  int nworst, 
  std::vector<std::string> from_list, 
  std::vector<std::vector<std::string>> through, 
  std::vector<std::string> to_list, 
  bool is_json);

std::vector<std::string> get_used_libs();

}  // namespace python_interface
