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
#include "feature_manager.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "congestion_eval.h"
#include "density_eval.h"
#include "feature_builder.h"
#include "feature_parser.h"

namespace ieda_feature {
namespace {

struct GCellPatch
{
  int32_t id = 0;
  int32_t row = 0;
  int32_t col = 0;
  int32_t lx = 0;
  int32_t ly = 0;
  int32_t ux = 0;
  int32_t uy = 0;
};

using PatchCoordMap = std::map<int, std::pair<std::pair<int, int>, std::pair<int, int>>>;

std::vector<GCellPatch> parseGCellInfo(const std::filesystem::path& gcell_path)
{
  std::vector<GCellPatch> patches;
  std::ifstream file(gcell_path);
  if (!file.is_open()) {
    return patches;
  }
  std::string line;
  int32_t max_col = -1;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    std::replace(line.begin(), line.end(), ',', ' ');
    std::istringstream iss(line);
    GCellPatch patch;
    if (!(iss >> patch.col >> patch.row >> patch.lx >> patch.ly >> patch.ux >> patch.uy)) {
      continue;
    }
    if (patch.ux <= patch.lx || patch.uy <= patch.ly) {
      continue;
    }
    max_col = std::max(max_col, patch.col);
    patches.push_back(patch);
  }
  const int32_t cols = max_col + 1;
  for (GCellPatch& patch : patches) {
    patch.id = patch.row * cols + patch.col;
  }
  std::sort(patches.begin(), patches.end(), [](const GCellPatch& lhs, const GCellPatch& rhs) {
    if (lhs.row != rhs.row) {
      return lhs.row < rhs.row;
    }
    return lhs.col < rhs.col;
  });
  return patches;
}

PatchCoordMap buildPatchCoordMap(const std::vector<GCellPatch>& patches)
{
  PatchCoordMap patch_coords;
  for (const GCellPatch& patch : patches) {
    patch_coords[patch.id] = {{patch.lx, patch.ly}, {patch.ux, patch.uy}};
  }
  return patch_coords;
}

std::pair<int32_t, int32_t> getShape(const std::vector<GCellPatch>& patches)
{
  int32_t rows = 0;
  int32_t cols = 0;
  for (const GCellPatch& patch : patches) {
    rows = std::max(rows, patch.row + 1);
    cols = std::max(cols, patch.col + 1);
  }
  return {rows, cols};
}

template <typename Value>
std::vector<std::vector<double>> toMatrix(const std::vector<GCellPatch>& patches, const std::map<int, Value>& values)
{
  auto [rows, cols] = getShape(patches);
  std::vector<std::vector<double>> matrix(rows, std::vector<double>(cols, 0.0));
  for (const GCellPatch& patch : patches) {
    auto iter = values.find(patch.id);
    if (iter != values.end()) {
      matrix[patch.row][patch.col] = static_cast<double>(iter->second);
    }
  }
  return matrix;
}

bool writeMatrixCSV(const std::filesystem::path& path, const std::vector<std::vector<double>>& matrix)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }
  for (const auto& row : matrix) {
    for (size_t col = 0; col < row.size(); ++col) {
      file << std::fixed << std::setprecision(6) << row[col];
      if (col + 1 < row.size()) {
        file << ",";
      }
    }
    file << "\n";
  }
  return true;
}

std::vector<ieval::DensityCell> filterCells(const std::vector<ieval::DensityCell>& cells, const std::string& type)
{
  if (type == "all") {
    return cells;
  }
  std::vector<ieval::DensityCell> filtered;
  std::copy_if(cells.begin(), cells.end(), std::back_inserter(filtered), [&type](const ieval::DensityCell& cell) {
    return cell.type == type;
  });
  return filtered;
}

std::vector<ieval::DensityPin> filterPins(const std::vector<ieval::DensityPin>& pins, const std::string& type)
{
  if (type == "all") {
    return pins;
  }
  std::vector<ieval::DensityPin> filtered;
  std::copy_if(pins.begin(), pins.end(), std::back_inserter(filtered), [&type](const ieval::DensityPin& pin) {
    return pin.type == type;
  });
  return filtered;
}

double overlapArea(const ieval::DensityNet& net, const GCellPatch& patch)
{
  const int32_t overlap_lx = std::max(net.lx, patch.lx);
  const int32_t overlap_ly = std::max(net.ly, patch.ly);
  const int32_t overlap_ux = std::min(net.ux, patch.ux);
  const int32_t overlap_uy = std::min(net.uy, patch.uy);
  return std::max(0, overlap_ux - overlap_lx) * std::max(0, overlap_uy - overlap_ly);
}

std::vector<ieval::DensityNet> filterNetsByLocality(const std::vector<ieval::DensityNet>& nets, const std::vector<GCellPatch>& patches,
                                                    bool local)
{
  std::vector<ieval::DensityNet> filtered;
  for (const ieval::DensityNet& net : nets) {
    int32_t overlap_count = 0;
    for (const GCellPatch& patch : patches) {
      if (overlapArea(net, patch) > 0) {
        overlap_count++;
        if (overlap_count > 1) {
          break;
        }
      }
    }
    if ((local && overlap_count <= 1) || (!local && overlap_count > 1)) {
      filtered.push_back(net);
    }
  }
  return filtered;
}

std::map<int, double> patchRUDY(const std::vector<GCellPatch>& patches, const ieval::CongestionNets& nets, const std::string& direction)
{
  std::map<int, double> values;
  for (const GCellPatch& patch : patches) {
    const double patch_area = static_cast<double>((patch.ux - patch.lx) * (patch.uy - patch.ly));
    double rudy = 0.0;
    if (patch_area <= 0) {
      values[patch.id] = 0.0;
      continue;
    }
    for (const ieval::CongestionNet& net : nets) {
      if (net.pins.empty()) {
        continue;
      }
      int32_t lx = INT32_MAX;
      int32_t ly = INT32_MAX;
      int32_t ux = INT32_MIN;
      int32_t uy = INT32_MIN;
      for (const ieval::CongestionPin& pin : net.pins) {
        lx = std::min(lx, pin.lx);
        ly = std::min(ly, pin.ly);
        ux = std::max(ux, pin.lx);
        uy = std::max(uy, pin.ly);
      }
      const int32_t overlap_lx = std::max(lx, patch.lx);
      const int32_t overlap_ly = std::max(ly, patch.ly);
      const int32_t overlap_ux = std::min(ux, patch.ux);
      const int32_t overlap_uy = std::min(uy, patch.uy);
      const double overlap = std::max(0, overlap_ux - overlap_lx) * std::max(0, overlap_uy - overlap_ly);
      if (overlap <= 0) {
        continue;
      }
      const double horizontal_rudy = (uy == ly) ? 1.0 : 1.0 / static_cast<double>(uy - ly);
      const double vertical_rudy = (ux == lx) ? 1.0 : 1.0 / static_cast<double>(ux - lx);
      if (direction == "horizontal") {
        rudy += overlap * horizontal_rudy / patch_area;
      } else if (direction == "vertical") {
        rudy += overlap * vertical_rudy / patch_area;
      } else {
        rudy += overlap * (horizontal_rudy + vertical_rudy) / patch_area;
      }
    }
    values[patch.id] = rudy;
  }
  return values;
}

std::map<int, double> patchMargin(const std::vector<GCellPatch>& patches, const std::vector<ieval::DensityCell>& cells,
                                  const ieval::DensityRegion& core, const std::string& direction)
{
  std::vector<ieval::DensityCell> macros = filterCells(cells, "macro");
  std::map<int, double> values;
  for (const GCellPatch& patch : patches) {
    if (patch.ux <= core.lx || patch.lx >= core.ux || patch.uy <= core.ly || patch.ly >= core.uy) {
      values[patch.id] = 0.0;
      continue;
    }
    const double patch_area = static_cast<double>((patch.ux - patch.lx) * (patch.uy - patch.ly));
    double macro_overlap = 0.0;
    for (const ieval::DensityCell& macro : macros) {
      ieval::DensityNet macro_box{macro.lx, macro.ly, macro.lx + macro.width, macro.ly + macro.height, macro.id};
      macro_overlap += overlapArea(macro_box, patch);
    }
    if (patch_area > 0 && macro_overlap > 0.5 * patch_area) {
      values[patch.id] = 0.0;
      continue;
    }
    int32_t h_right = core.ux;
    int32_t h_left = core.lx;
    int32_t v_up = core.uy;
    int32_t v_down = core.ly;
    const double grid_middle_x = (patch.lx + patch.ux) * 0.5;
    const double grid_middle_y = (patch.ly + patch.uy) * 0.5;
    for (const ieval::DensityCell& macro : macros) {
      const double macro_middle_x = macro.lx + macro.width * 0.5;
      const double macro_middle_y = macro.ly + macro.height * 0.5;
      if (grid_middle_y >= macro.ly && grid_middle_y <= macro.ly + macro.height) {
        if (macro_middle_x > grid_middle_x) {
          h_right = std::min(h_right, macro.lx);
        } else {
          h_left = std::max(h_left, macro.lx + macro.width);
        }
      }
      if (grid_middle_x >= macro.lx && grid_middle_x <= macro.lx + macro.width) {
        if (macro_middle_y > grid_middle_y) {
          v_up = std::min(v_up, macro.ly);
        } else {
          v_down = std::max(v_down, macro.ly + macro.height);
        }
      }
    }
    const double horizontal = h_right - h_left;
    const double vertical = v_up - v_down;
    values[patch.id] = direction == "horizontal" ? horizontal : direction == "vertical" ? vertical : horizontal + vertical;
  }
  return values;
}

}  // namespace

FeatureManager* FeatureManager::_instance = nullptr;

FeatureManager::FeatureManager()
{
  _summary = new FeatureSummary();
}
FeatureManager::~FeatureManager()
{
  if (_summary != nullptr) {
    delete _summary;
    _summary = nullptr;
  }
}

bool FeatureManager::save_summary(std::string path)
{
  FeatureBuilder builder;
  auto db_summary = builder.buildDBSummary();

  _summary->set_db(db_summary);

  FeatureParser feature_parser(_summary);
  return feature_parser.buildSummary(path);
}

bool FeatureManager::save_eval_summary(std::string path, int32_t grid_size)
{
  FeatureBuilder builder;

  auto wirelength_db = builder.buildWirelengthEvalSummary();
  auto density_db = builder.buildDensityEvalSummary(grid_size);
  auto congestion_db = builder.buildCongestionEvalSummary(grid_size);
  auto timing_db = builder.buildTimingEvalSummary();

  _summary->set_wirelength_eval(wirelength_db);
  _summary->set_density_eval(density_db);
  _summary->set_congestion_eval(congestion_db);
  _summary->set_timing_eval(timing_db);

  FeatureParser feature_parser(_summary);
  return feature_parser.buildSummaryEval(path);
}

bool FeatureManager::save_eval_union(std::string jsonl_path, std::string csv_path, int32_t grid_size)
{
  FeatureBuilder builder;

  bool is_init_eval_tool = builder.initEvalTool();
  if (!is_init_eval_tool) {
    return false;
  }

  // auto union_db = builder.buildUnionEvalSummary(grid_size);
  // _summary->set_wirelength_eval(union_db.total_wl_summary);
  // _summary->set_density_eval(union_db.density_map_summary);
  // _summary->set_congestion_eval(union_db.congestion_summary);

  bool csv_success = builder.buildNetEval(csv_path);
  // bool csv_success = true;

  // FeatureParser feature_parser(_summary);
  // bool jsonl_success = feature_parser.buildSummaryEval(jsonl_path);
  bool jsonl_success = true;

  builder.destroyEvalTool();

  return jsonl_success && csv_success;
}

bool FeatureManager::save_pl_eval(std::string json_path, int32_t grid_size)
{
  // EGR
  FeatureBuilder builder;

  bool is_init_eval_tool = builder.initEvalTool();
  if (!is_init_eval_tool) {
    return false;
  }

  std::string stage = "place";

  auto union_db = builder.buildUnionEvalSummary(grid_size, stage);
  _summary->set_wirelength_eval(union_db.total_wl_summary);
  _summary->set_density_eval(union_db.density_map_summary);
  _summary->set_congestion_eval(union_db.congestion_summary);

  // builder.evalTiming("EGR", true);
  // builder.evalTiming("HPWL");
  // builder.evalTiming("FLUTE");
  // builder.evalTiming("SALT");

  // auto union_timing_db = builder.buildTimingUnionEvalSummary();
  // _summary->set_timing_eval(union_timing_db);

  FeatureParser feature_parser(_summary);
  bool json_success = feature_parser.buildSummaryEval(json_path);
  bool gcell_patch_success = save_gcell_patch_eval(json_path, stage);

  builder.destroyEvalTool();

  return json_success && gcell_patch_success;
}

bool FeatureManager::save_cts_eval(std::string json_path, int32_t grid_size)
{
  // EGR
  FeatureBuilder builder;

  bool is_init_eval_tool = builder.initEvalTool();
  if (!is_init_eval_tool) {
    return false;
  }

  std::string stage = "cts";

  auto union_db = builder.buildUnionEvalSummary(grid_size, stage);
  _summary->set_wirelength_eval(union_db.total_wl_summary);
  _summary->set_density_eval(union_db.density_map_summary);
  _summary->set_congestion_eval(union_db.congestion_summary);

  // builder.evalTiming("EGR", true);

  // builder.evalTiming("HPWL");
  // builder.evalTiming("FLUTE");
  // builder.evalTiming("SALT");

  // auto union_timing_db = builder.buildTimingUnionEvalSummary();
  // _summary->set_timing_eval(union_timing_db);

  FeatureParser feature_parser(_summary);
  bool json_success = feature_parser.buildSummaryEval(json_path);
  bool gcell_patch_success = save_gcell_patch_eval(json_path, stage);

  builder.destroyEvalTool();

  return json_success && gcell_patch_success;
}

bool FeatureManager::save_gcell_patch_eval(std::string json_path, std::string stage)
{
  std::filesystem::path feature_dir = std::filesystem::path(json_path).parent_path();
  std::filesystem::path stage_dir = feature_dir.parent_path();
  std::filesystem::path gcell_path = stage_dir / "data" / "rt" / "rt_temp_directory" / "early_router" / "gcell.info";
  std::vector<GCellPatch> patches = parseGCellInfo(gcell_path);
  if (patches.empty()) {
    return true;
  }

  PatchCoordMap patch_coords = buildPatchCoordMap(patches);
  std::filesystem::path output_root = feature_dir / "gcell_patch_map";

  ieval::DensityEval* density_eval_inst = ieval::DensityEval::getInst();
  density_eval_inst->initIDB();
  ieval::DensityEval density_eval;
  std::vector<ieval::DensityCell> cells = density_eval_inst->getDensityCells();
  std::vector<ieval::DensityPin> pins = density_eval_inst->getDensityPins();
  std::vector<ieval::DensityNet> nets = density_eval_inst->getDensityNets();
  ieval::DensityRegion core = density_eval_inst->getDensityRegionCore();

  bool success = true;
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_allcell_density.csv"),
                            toMatrix(patches, density_eval.patchCellDensity(filterCells(cells, "all"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_macro_density.csv"),
                            toMatrix(patches, density_eval.patchCellDensity(filterCells(cells, "macro"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_stdcell_density.csv"),
                            toMatrix(patches, density_eval.patchCellDensity(filterCells(cells, "stdcell"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_allcell_pin_density.csv"),
                            toMatrix(patches, density_eval.patchPinDensity(filterPins(pins, "all"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_macro_pin_density.csv"),
                            toMatrix(patches, density_eval.patchPinDensity(filterPins(pins, "macro"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_stdcell_pin_density.csv"),
                            toMatrix(patches, density_eval.patchPinDensity(filterPins(pins, "stdcell"), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_allnet_density.csv"),
                            toMatrix(patches, density_eval.patchNetDensity(nets, patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_local_net_density.csv"),
                            toMatrix(patches, density_eval.patchNetDensity(filterNetsByLocality(nets, patches, true), patch_coords)));
  success &= writeMatrixCSV(output_root / "density_map" / (stage + "_global_net_density.csv"),
                            toMatrix(patches, density_eval.patchNetDensity(filterNetsByLocality(nets, patches, false), patch_coords)));

  success &= writeMatrixCSV(output_root / "margin_map" / (stage + "_horizontal_margin.csv"), toMatrix(patches, patchMargin(patches, cells, core, "horizontal")));
  success &= writeMatrixCSV(output_root / "margin_map" / (stage + "_vertical_margin.csv"), toMatrix(patches, patchMargin(patches, cells, core, "vertical")));
  success &= writeMatrixCSV(output_root / "margin_map" / (stage + "_union_margin.csv"), toMatrix(patches, patchMargin(patches, cells, core, "union")));

  ieval::CongestionEval* congestion_eval_inst = ieval::CongestionEval::getInst();
  congestion_eval_inst->initIDB();
  ieval::CongestionNets congestion_nets = congestion_eval_inst->getCongestionNets();
  success &= writeMatrixCSV(output_root / "RUDY_map" / (stage + "_rudy_horizontal.csv"), toMatrix(patches, patchRUDY(patches, congestion_nets, "horizontal")));
  success &= writeMatrixCSV(output_root / "RUDY_map" / (stage + "_rudy_vertical.csv"), toMatrix(patches, patchRUDY(patches, congestion_nets, "vertical")));
  success &= writeMatrixCSV(output_root / "RUDY_map" / (stage + "_rudy_union.csv"), toMatrix(patches, patchRUDY(patches, congestion_nets, "union")));
  congestion_eval_inst->destroyIDB();

  return success;
}

bool FeatureManager::save_timing_eval_summary(std::string path)
{
  FeatureBuilder builder;
  auto eval_db = builder.buildTimingEvalSummary();

  _summary->set_timing_eval(eval_db);

  FeatureParser feature_parser(_summary);
  return feature_parser.buildSummaryTimingEval(path);
}

bool FeatureManager::save_tools(std::string path, std::string step)
{
  FeatureBuilder builder;
  if (step == "fixFanout") {
    auto db = builder.buildNetOptSummary();

    _summary->set_ino(db);
  } else if (step == "place" || step == "legalization" || (step == "filler")) {
    auto db = builder.buildPLSummary(step);

    _summary->set_ipl(db);
  } else if (step == "CTS") {
    auto db = builder.buildCTSSummary();

    _summary->set_icts(db);
  } else if (step == "optDrv") {
    auto db = builder.buildTimingOptSummary();

    _summary->set_ito_optdrv(db);
  } else if (step == "optHold") {
    auto db = builder.buildTimingOptSummary();

    _summary->set_ito_opthold(db);
  } else if (step == "optSetup") {
    auto db = builder.buildTimingOptSummary();

    _summary->set_ito_optsetup(db);
  } else if (step == "route") {
    // skip
  } else {
  }

  FeatureParser feature_parser(_summary);
  return feature_parser.buildTools(path, step);
}

bool FeatureManager::save_eval_map(std::string path, int bin_cnt_x, int bin_cnt_y)
{
  FeatureParser feature_parser(_summary);
  return feature_parser.buildSummaryMap(path, bin_cnt_x, bin_cnt_y);
}

bool FeatureManager::save_net_eval(std::string path)
{
  FeatureParser feature_parser;
  return feature_parser.buildNetEval(path);
}

bool FeatureManager::save_route_data(std::string path)
{
  FeatureBuilder builder;
  builder.buildRouteData(&_route_data);

  FeatureParser feature_parser(_summary);
  return feature_parser.buildRouteData(path, &_route_data);
}

bool FeatureManager::read_route_data(std::string path)
{
  FeatureParser feature_parser;
  return feature_parser.readRouteData(path, &_route_data);
}

bool FeatureManager::feature_macro_drc(std::string path, std::string drc_path)
{
  FeatureParser feature_parser;
  return feature_parser.buildMacroDrc(path, drc_path);
}

bool FeatureManager::save_cong_map(std::string stage, std::string csv_dir)
{
  FeatureParser feature_parser;
  return feature_parser.buildCongMap(stage, csv_dir);
}
}  // namespace ieda_feature
