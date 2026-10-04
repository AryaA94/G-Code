#include "gcodesim/materials.hpp"

namespace gcodesim {

const std::vector<Material>& materials() {
  // Ranges are typical published starting values for carbide; the lint rules
  // allow some slack around them.
  static const std::vector<Material> list = {
      {"aluminum_6061", "Aluminum 6061", 250, 800, 0.008, 0.020},
      {"aluminum_7075", "Aluminum 7075", 200, 600, 0.008, 0.018},
      {"mild_steel", "Mild steel (A36, 1018)", 100, 250, 0.005, 0.012},
      {"alloy_steel", "Alloy steel, Q&T (4140, HS 100)", 70, 150, 0.004, 0.010},
      {"ar400", "Abrasion-resistant plate, ~400 BHN (AR400)", 45, 90, 0.003, 0.007},
      {"ar500", "Abrasion-resistant plate, ~500 BHN (AR500)", 35, 70, 0.0025, 0.006},
      {"hardened_600", "Hardened plate, ~600 BHN", 25, 50, 0.002, 0.005},
      {"stainless_304", "Stainless 304", 70, 150, 0.004, 0.009},
      {"titanium_ti6al4v", "Titanium Ti-6Al-4V (TC4)", 40, 80, 0.004, 0.009},
  };
  return list;
}

const Material* find_material(const std::string& key) {
  for (const auto& m : materials())
    if (m.key == key) return &m;
  return nullptr;
}

}  // namespace gcodesim
