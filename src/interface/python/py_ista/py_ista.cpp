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
#include "py_ista.h"

#include <tool_manager.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "idm.h"
#include "log/Log.hh"
#include "sta/Sta.hh"
#include "timing_api.hh"
namespace python_interface {

namespace {

ista::AnalysisMode parseTimingModelAnalysisMode(std::string analysis_mode)
{
  std::transform(analysis_mode.begin(), analysis_mode.end(),
                 analysis_mode.begin(), [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });

  if (analysis_mode.empty() || analysis_mode == "max") {
    return ista::AnalysisMode::kMax;
  }
  if (analysis_mode == "min") {
    return ista::AnalysisMode::kMin;
  }

  throw std::invalid_argument(
      "write_timing_model only supports analysis_mode 'max' or 'min'");
}

}  // namespace

bool staRun(const std::string& output)
{
  bool run_ok = iplf::tmInst->autoRunSTA(output);
  return run_ok;
}

bool staInit(const std::string& output)
{
  bool run_ok = iplf::tmInst->initSTA(output);
  return run_ok;
}

bool staReport(const std::string& output)
{
  bool run_ok = iplf::tmInst->runSTA(output);
  return run_ok;
}

bool setDesignWorkSpace(const std::string& design_workspace)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->set_design_work_space(design_workspace.c_str());
  return true;
}

bool read_lef_def(std::vector<std::string>& lef_files, const std::string& def_file)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();

  timing_engine->readDefDesign(def_file, lef_files);

  return 1;
}

bool readVerilog(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();

  ista->readVerilogWithRustParser(file_name.c_str());
  return true;
}

bool readLiberty(std::vector<std::string>& lib_files)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->readLiberty(lib_files);
  return true;
}

bool linkDesign(const std::string& cell_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->set_top_module_name(cell_name.c_str());
  ista->linkDesignWithRustParser(cell_name.c_str());
  return true;
}

bool readSpef(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->readSpef(file_name.c_str());
  return true;
}

bool readSdc(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  return ista->readSdc(file_name.c_str());
}

std::string getNetName(const std::string& pin_port_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto objs = ista->get_netlist()->findObj(pin_port_name.c_str(), false, false);
  LOG_FATAL_IF(objs.size() != 1);

  auto* pin_or_port = objs[0];
  std::string net_name = pin_or_port->get_net()->get_name();

  return net_name;
}

double getSegmentResistance(int layer_id, double segment_length, int route_layer_id)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* idb_adapter = dynamic_cast<ista::TimingIDBAdapter*>(timing_engine->get_db_adapter());
  double resistance = idb_adapter->getResistance(layer_id, segment_length, std::nullopt, route_layer_id);

  return resistance;
}

double getSegmentCapacitance(int layer_id, double segment_length, int route_layer_id)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* idb_adapter = dynamic_cast<ista::TimingIDBAdapter*>(timing_engine->get_db_adapter());
  double capacitance = idb_adapter->getCapacitance(layer_id, segment_length, std::nullopt, route_layer_id);

  return capacitance;
}

std::string makeRCTreeInnerNode(const std::string& net_name, int id, float cap)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  auto* rc_node = timing_engine->makeOrFindRCTreeNode(the_net, id);
  rc_node->incrCap(cap);

  return rc_node->get_name();
}

std::string makeRCTreeObjNode(const std::string& pin_port_name, float cap)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();

  auto the_pin_ports = ista->get_netlist()->findObj(pin_port_name.c_str(), false, false);
  assert(the_pin_ports.size() == 1);

  auto* rc_node = timing_engine->makeOrFindRCTreeNode(the_pin_ports.front());
  rc_node->incrCap(cap);

  return rc_node->get_name();
}

bool makeRCTreeEdge(const std::string& net_name, std::string& node1, std::string& node2, float res)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  auto* rc_node1 = timing_engine->findRCTreeNode(the_net, node1);
  auto* rc_node2 = timing_engine->findRCTreeNode(the_net, node2);

  timing_engine->makeResistor(the_net, rc_node1, rc_node2, res);

  return true;
}

bool updateRCTreeInfo(const std::string& net_name)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  timing_engine->updateRCTreeInfo(the_net);

  return true;
}

bool updateTiming()
{
  auto* ista = ista::Sta::getOrCreateSta();
  if (!ista->isBuildGraph()) {
    ista->buildGraph();
  }
  ista->updateTiming();
  return true;
}

bool buildTimingRcTree(const std::string& routing_type)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  if (!ista->isBuildGraph()) {
    timing_engine->buildGraph();
    timing_engine->initRcTree();
  }
  TimingPower_API_INST->buildRCTree(routing_type);
  return true;
}

bool writeTimingModel(const std::string& output_lib_path,
                      const std::string& analysis_mode)
{
  namespace fs = std::filesystem;

  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  const auto mode = parseTimingModelAnalysisMode(analysis_mode);

  const fs::path output_lib(output_lib_path);
  if (!output_lib.parent_path().empty()) {
    std::error_code ec;
    fs::create_directories(output_lib.parent_path(), ec);
    if (ec) {
      throw std::runtime_error("failed to create output directory: " +
                               output_lib.parent_path().string() + ", error=" +
                               ec.message());
    }
    const auto workspace = output_lib.parent_path().string();
    timing_engine->set_design_work_space(workspace.c_str());
  }

  timing_engine->get_ista()->set_analysis_mode(mode);
  timing_engine->extractTimingModel(mode, output_lib_path.c_str());

  return fs::exists(output_lib) && fs::file_size(output_lib) > 0;
}

bool reportSta()
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->reportTiming();
  return true;
}

std::vector<PathWireTimingData> getWireTimingData(unsigned n_worst_path_per_clock)
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto path_wire_timing_data = ista->reportTimingData(n_worst_path_per_clock);

  std::vector<PathWireTimingData> ret_timing_data;

  for (auto& one_path_wire_timing_data : path_wire_timing_data) {
    PathWireTimingData ret_one_path_data;
    for (auto& wire_timing_data : one_path_wire_timing_data) {
      WireTimingData ret_wire_data;
      ret_wire_data._from_node_name = std::move(wire_timing_data._from_node_name);
      ret_wire_data._to_node_name = std::move(wire_timing_data._to_node_name);
      ret_wire_data._wire_resistance = wire_timing_data._wire_resistance;
      ret_wire_data._wire_capacitance = wire_timing_data._wire_capacitance;
      ret_wire_data._wire_delay = wire_timing_data._wire_delay;
      ret_wire_data._wire_from_slew = wire_timing_data._wire_from_slew;
      ret_wire_data._wire_to_slew = wire_timing_data._wire_to_slew;

      ret_one_path_data.emplace_back(std::move(ret_wire_data));
    }

    ret_timing_data.emplace_back(std::move(ret_one_path_data));
  }

  LOG_INFO << "get wire data size: " << ret_timing_data.size();

  return ret_timing_data;
}

bool reportTiming(int digits, const std::string& delay_type, std::vector<std::string> exclude_cell_names, bool derate, bool is_clock_cap,
                  bool is_not_bak_rpt, int max_path, int nworst, std::vector<std::string> from_list,
                  std::vector<std::vector<std::string>> through, std::vector<std::string> to_list, bool is_json)
{
  // Get Sta instance
  auto* ista = ista::Sta::getOrCreateSta();

  // Set significant digits
  ista->set_significant_digits(digits);

  // Set analysis mode
  if (!delay_type.empty()) {
    if (delay_type == "max_min") {
      ista->set_analysis_mode(ista::AnalysisMode::kMaxMin);
    } else if (delay_type == "max") {
      ista->set_analysis_mode(ista::AnalysisMode::kMax);
    } else if (delay_type == "min") {
      ista->set_analysis_mode(ista::AnalysisMode::kMin);
    }
  }

  // Set max path per clock
  ista->set_n_worst_path_per_clock(max_path);

  // Set max path per endpoint
  ista->set_n_worst_path_per_endpoint(nworst);

  // Set report spec
  ista->setReportSpec(std::move(from_list), std::move(through), std::move(to_list));

  // Enable json report if needed
  if (is_json) {
    ista->enableJsonReport();
  }

  // Build graph
  if (!ista->isBuildGraph()) {
    ista->buildGraph();
  }

  // Update timing
  ista->updateTiming();

  // Convert exclude_cell_names to set
  std::set<std::string> exclude_cell_names_set;
  for (const auto& exclude_cell_name : exclude_cell_names) {
    exclude_cell_names_set.insert(exclude_cell_name);
  }

  // Report timing
  ista->reportTiming(std::move(exclude_cell_names_set), derate, is_clock_cap, is_not_bak_rpt);

  return true;
}

void build_timing_graph()
{
  // Get Sta instance
  auto* ista = ista::Sta::getOrCreateSta();
  // Build graph
  ista->buildGraph();
}

void update_clock_timing()
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->updateClockTiming();
}

void buildRcTreeFromFlatData(const std::string& netName, const std::vector<std::string>& node_sta_names,
                             const std::vector<bool>& node_is_pin, const std::vector<int>& steiner_indices,
                             const std::vector<int>& parent_indices, const std::vector<double>& node_total_caps,
                             const std::vector<double>& edge_resistances, const std::vector<int>& node_global_indices)
{
  auto& timingEngine = *(ista::TimingEngine::getOrCreateTimingEngine());

  ista::Net* staNet = timingEngine.get_netlist()->findNet(netName.c_str());

  if (!staNet) {
    LOG_WARNING << "在STA引擎中找不到网络(py_ista.cpp): " << netName;
    return;
  }
  timingEngine.resetRcTree(staNet);

  std::vector<ista::RctNode*> rct_nodes(node_sta_names.size());
  for (size_t i = 0; i < node_sta_names.size(); ++i) {
    ista::DesignObject* pinObject = nullptr;
    if (node_is_pin[i]) {
      // 是真实引脚
      auto pinObjects = timingEngine.get_netlist()->findObj(node_sta_names[i].c_str(), false, false);
      if (pinObjects.empty()) {
        LOG_WARNING << "Pin not found(py_ista.cpp): " << node_sta_names[i];
        rct_nodes[i] = nullptr;
        continue;
      }
      pinObject = pinObjects.front();
      if (pinObject) {
        rct_nodes[i] = timingEngine.makeOrFindRCTreeNode(pinObject);
      }
    } else {
      // 是Steiner点
      rct_nodes[i] = timingEngine.makeOrFindRCTreeNode(staNet, steiner_indices[i]);
    }
    if (rct_nodes[i]) {
      timingEngine.incrCap(rct_nodes[i], node_total_caps[i], false);
      // *** 补充初始化1：将RctNode与其原始DesignObject关联起来 ***
      // 这对于从RC树节点反向查询到设计对象非常重要。
      // 斯坦纳点没有对应的DesignObject，所以pinObject为nullptr。
      rct_nodes[i]->set_obj(pinObject);
    }
  }

  // 设置RC树的根节点
  auto root_it = std::find(parent_indices.begin(), parent_indices.end(), -1);
  if (root_it != parent_indices.end()) {
    int root_local_idx = std::distance(parent_indices.begin(), root_it);
    ista::RctNode* root_node = rct_nodes[root_local_idx];

    if (root_node) {
      auto* rc_net = timingEngine.get_ista()->getRcNet(staNet);
      if (rc_net && rc_net->rct()) {
        rc_net->rct()->set_root(root_node);
      } else {
        LOG_WARNING << "无法为网络 " << netName << " 获取RcTree对象以设置根节点。";
      }
    }
  } else {
    LOG_WARNING << "在网络 " << netName << " 中未找到根节点（父节点索引为-1）。";
  }

  // 创建电阻并建立父子关系
  for (size_t i = 0; i < parent_indices.size(); ++i) {
    int parent_idx = parent_indices[i];
    if (parent_idx == -1) {
      continue;
    }

    ista::RctNode* child_node = rct_nodes[i];
    ista::RctNode* parent_node = rct_nodes[parent_idx];

    if (child_node && parent_node) {
      timingEngine.makeResistor(staNet, parent_node, child_node, edge_resistances[i]);

      // *** 补充初始化2：显式设置每个节点的父节点指针 ***
      // 这使得RC树的层级结构更加明确和完整。
      child_node->set_parent(parent_node);
    }
  }
  timingEngine.updateRCTreeInfo(staNet);
}

void collectRctDataAndFillList(ista::RctNode* node, ista::Net* net, pybind11::list& results_list,
                               std::unordered_set<ista::RctNode*>& visited)
{  // ★ 移除 input_slew_ns 参数

  if (!node || visited.count(node)) {
    return;
  }
  visited.insert(node);

  if (auto* pin_obj = dynamic_cast<ista::Pin*>(node->get_obj())) {
    std::string full_name = pin_obj->getFullName();  // 假设getFullName()返回"inst:pin"

    // ★ 从Slew银行获取数据
    const auto& slew_map = SlewDebugDataManager::getInstance().getData();

    FOREACH_MODE_TRANS(mode, trans)
    {
      ista::ModeTransPair key_elmore(mode, trans);

      // ★ 构建用于查询Slew银行的key
      SlewKey key_slew;
      key_slew.pin_full_name = full_name;
      key_slew.mode = mode;
      key_slew.trans = trans;

      // ★ 从Slew银行查找官方Slew值，如果找不到则为NaN
      double official_slew_ns = std::numeric_limits<double>::quiet_NaN();
      auto it = slew_map.find(key_slew);
      if (it != slew_map.end()) {
        official_slew_ns = it->second;
      }

      pybind11::dict net_info_py;
      net_info_py["net_name"] = net->get_name();
      net_info_py["pin_name"] = full_name;
      if (full_name.find("_21692_:A") != std::string::npos) {
        std::cout << "Processing pin: " << full_name << " with impulse " << node->get_impulse()[key_elmore] << " and official_slew_ns "
                  << official_slew_ns << std::endl;
      }
      net_info_py["mode"] = (mode == ista::AnalysisMode::kMax) ? "Max" : "Min";
      net_info_py["transition"] = (trans == ista::TransType::kRise) ? "Rise" : "Fall";
      net_info_py["load"] = node->get_nload()[key_elmore];
      net_info_py["delay"] = node->get_ndelay()[key_elmore];
      net_info_py["slew_ns"] = official_slew_ns;  // ★ 使用官方Slew值
      net_info_py["ldelay"] = node->get_ldelay()[key_elmore];
      net_info_py["beta"] = node->get_beta()[key_elmore];
      net_info_py["impulse"] = node->get_impulse()[key_elmore];

      results_list.append(net_info_py);
    }
  }

  // ★ 移除递归传递Slew的逻辑
  for (auto* edge : node->get_fanout()) {
    if (edge) {
      collectRctDataAndFillList(&(edge->get_to()), net, results_list, visited);
    }
  }
}

void updateAndGetAllPinTimings(const std::vector<std::string>& pin_names, pybind11::list& arrival_late_times,
                               pybind11::list& arrival_early_times, pybind11::list& required_late_times,
                               pybind11::list& required_early_times, pybind11::list& pin_net_delay, pybind11::list& cell_arc_delays,
                               pybind11::list& net_timing_details)
{
  auto& timingEngine = *(ista::TimingEngine::getOrCreateTimingEngine());

  ArcDebugDataManager::getInstance().clear();
  SlewDebugDataManager::getInstance().clear();
  // 1. 执行完整的时序分析，这会构建并计算所有RC树
  // timingEngine.updateClockTiming();
  timingEngine.updateTiming();

  // 2. (保留) 您原有的获取AT/RT和Net Delay的逻辑
  for (const std::string& pin_name : pin_names) {
    // DEBUG
    if (pin_name.find("uio_oe[1]_reg") != std::string::npos) {
      LOG_INFO << "Processing pin: " << pin_name;
    }
    // auto* timingEngine = (ista::TimingEngine::getOrCreateTimingEngine());
    // DEBUG
    //  auto rise_value = STA_INST->getAT(pin_name.c_str(), ista::AnalysisMode::kMin, ista::TransType::kRise);
    auto rise_max_at = timingEngine.getAT(pin_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise);
    auto fall_max_at = timingEngine.getAT(pin_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall);
    auto rise_min_rt = timingEngine.getRT(pin_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise);
    auto fall_min_rt = timingEngine.getRT(pin_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall);

    // double at_late = timingEngine->get// ieval::TimingAPI::getInst()->getArrivalLateTime(pin_name);
    // double at_early = ieval::TimingAPI::getInst()->getArrivalEarlyTime(pin_name);
    pybind11::list at_max_list;
    at_max_list.append(rise_max_at.has_value() ? rise_max_at.value() : 0);
    at_max_list.append(fall_max_at.has_value() ? fall_max_at.value() : 0);

    pybind11::list rt_max_list;
    rt_max_list.append(rise_min_rt.has_value() ? rise_min_rt.value() : std::numeric_limits<double>::infinity());
    rt_max_list.append(fall_min_rt.has_value() ? fall_min_rt.value() : std::numeric_limits<double>::infinity());

    double at_early = ieval::TimingAPI::getInst()->getArrivalEarlyTime(pin_name);
    double rt_early = ieval::TimingAPI::getInst()->getRequiredEarlyTime(pin_name);

    arrival_late_times.append(at_max_list);
    required_late_times.append(rt_max_list);

    arrival_early_times.append(at_early);
    required_early_times.append(rt_early);

    auto pinObjects = timingEngine.get_netlist()->findObj(pin_name.c_str(), false, false);
    if (pinObjects.empty()) {
      pin_net_delay.append(std::numeric_limits<double>::quiet_NaN());
      continue;
    }
    ista::DesignObject* pinObject = pinObjects.front();
    ista::RctNode* rct_node = timingEngine.makeOrFindRCTreeNode(pinObject);
    if (rct_node) {
      pin_net_delay.append(rct_node->delay());  // 假设有 delay() getter
    } else {
      pin_net_delay.append(std::numeric_limits<double>::quiet_NaN());
    }
  }

  // 3. (保留) 您原有的获取Cell Arc Delay的逻辑
  const auto& all_arc_info = ArcDebugDataManager::getInstance().getArcInfo();
  for (const auto& info : all_arc_info) {
    pybind11::dict arc_info_py;
    arc_info_py["inst_name"] = info.inst_name;
    arc_info_py["from_pin"] = info.from_pin;
    arc_info_py["to_pin"] = info.to_pin;
    arc_info_py["analysis_mode"] = info.analysis_mode;
    arc_info_py["transition"] = info.transition;
    arc_info_py["in_slew_ns"] = info.in_slew_ns;
    arc_info_py["load_cap"] = info.load_cap;
    arc_info_py["delay_ns"] = info.delay_ns;
    arc_info_py["arc_sense"] = info.timing_sense;
    cell_arc_delays.append(arc_info_py);
  }

  // ================================================================
  // ***** 新增核心逻辑：遍历所有Net，提取RC树的详细信息 *****
  // ================================================================
  auto* netlist = timingEngine.get_netlist();
  Net* net;
  if (netlist) {
    // 遍历设计中的每一条Net
    FOREACH_NET(netlist, net)
    {
      if (!net)
        continue;
      std::string netName = net->get_name();
      ista::Net* staNet = timingEngine.get_netlist()->findNet(netName.c_str());

      // 获取该Net对应的RC Tree
      auto* rc_net = timingEngine.get_ista()->getRcNet(staNet);
      if (!rc_net)
        continue;
      auto* rct = rc_net->rct();
      if (!rct || !rct->get_root())
        continue;

      // 从根节点开始遍历整棵RC树，并直接将信息填充到 net_timing_details
      std::unordered_set<ista::RctNode*> visited_nodes;
      collectRctDataAndFillList(rct->get_root(), staNet, net_timing_details, visited_nodes);
      // collectRctDataAndFillList(rct->get_root(), staNet, net_timing_details, visited_nodes, initial_slew_ns);
    }
  }
}

std::vector<std::string> get_used_libs()
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto used_libs = ista->getUsedLibs();

  std::vector<std::string> ret;
  for (auto& lib : used_libs) {
    ret.push_back(lib->get_file_name());
  }

  return ret;
}

bool initLog(std::string log_dir)
{
  char config[] = "test";
  char* argv[] = {config};

  ieda::Log::makeSureDirectoryExist(log_dir);
  ieda::Log::init(argv, log_dir);

  return true;
}

void convertDBToTimingNetlist()
{
  auto timing_engine = ista::TimingEngine::getOrCreateTimingEngine();

  auto idb_adapter = std::make_unique<idb::TimingIDBAdapter>(timing_engine->get_ista());

  idb::IdbBuilder* idb = dmInst->get_idb_builder();
  idb_adapter->set_idb(idb);
  idb_adapter->convertDBToTimingNetlist(true);
  timing_engine->set_db_adapter(std::move(idb_adapter));
}
}  // namespace python_interface
