#include "res/rsm.h"

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void PutFloat(std::vector<std::uint8_t>& out, float f) {
  Put32(out, std::bit_cast<std::uint32_t>(f));
}

void PutName(std::vector<std::uint8_t>& out, const char* name) {
  std::vector<std::uint8_t> record(40, 0);
  std::memcpy(record.data(), name, std::strlen(name));
  out.insert(out.end(), record.begin(), record.end());
}

// A model of the given minor version: textures "a.bmp" and "b.bmp", a root node "root"
// translated by (10, 0, 0) with one triangle in the z = 0 plane, and a child "arm" with
// no mesh.
std::vector<std::uint8_t> Rsm(std::uint8_t minor) {
  std::vector<std::uint8_t> out = {'G', 'R', 'S', 'M', 1, minor};
  Put32(out, 300);  // animation length
  Put32(out, 1);
  if (minor >= 4) out.push_back(128);
  out.resize(out.size() + 16, 0);
  Put32(out, 2);
  PutName(out, "a.bmp");
  PutName(out, "b.bmp");
  PutName(out, "root");
  Put32(out, 2);
  for (const char* name : {"root", "arm"}) {
    const bool root = std::strcmp(name, "root") == 0;
    PutName(out, name);
    PutName(out, root ? "" : "root");
    Put32(out, 1);
    Put32(out, root ? 1 : 0);  // texture b.bmp or a.bmp
    for (float f : {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}) PutFloat(out, f);
    for (float f : {10.0f, 0.0f, 0.0f}) PutFloat(out, f);
    for (int i = 0; i < 7; ++i) PutFloat(out, 1.0f);  // position, angle, axis
    for (int i = 0; i < 3; ++i) PutFloat(out, 2.0f);  // scale
    const int vertices = root ? 3 : 0;
    Put32(out, static_cast<std::uint32_t>(vertices));
    const float points[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (int v = 0; v < vertices; ++v) {
      for (float f : points[v]) PutFloat(out, f);
    }
    Put32(out, 0);  // texture coordinates
    Put32(out, root ? 1 : 0);
    if (root) {
      for (std::uint32_t v : {0u, 1u, 2u, 0u, 0u, 0u, 0u}) Put16(out, v);
      if (minor == 1) {
        out.resize(out.size() + 6, 0);
      } else {
        Put16(out, 0);
        Put32(out, 0);
        Put32(out, 3);  // smoothing group
      }
    }
    if (minor >= 5) Put32(out, 0);
    Put32(out, 0);  // rotation keys
  }
  if (minor < 5) {
    Put32(out, 1);
    Put32(out, 7);
    for (float f : {1.0f, 2.0f, 3.0f}) PutFloat(out, f);
  }
  Put32(out, 1);  // volumes
  if (minor >= 2) {
    for (int i = 0; i < 9; ++i) PutFloat(out, static_cast<float>(i));
  }
  if (minor >= 3) Put32(out, 0);
  return out;
}

ModelFace Face(std::uint16_t texture, std::uint16_t tag) {
  ModelFace face;
  face.texture = texture;
  face.vertices = {tag, 0, 0};
  face.field_10 = tag;
  return face;
}

}  // namespace

TEST_CASE("a model: nodes, links, the baked matrix, normals") {
  for (int minor : {1, 2, 3, 4, 5}) {
    INFO("minor " << minor);
    Model model;
    std::string error;
    REQUIRE(DecodeModel(Rsm(static_cast<std::uint8_t>(minor)), model, &error));
    CHECK(model.animation_length == 300);
    CHECK(model.alpha == (minor >= 4 ? 128 : 255));
    CHECK(model.root == "root");
    REQUIRE(model.nodes.size() == 2);
    const ModelNode& root = model.nodes[0];
    const ModelNode& arm = model.nodes[1];
    CHECK(root.parent == -1);
    CHECK(root.children == std::vector<int>{1});
    CHECK(arm.parent == 0);
    CHECK(model.textures[static_cast<std::size_t>(root.textures[0])] == "b.bmp");
    CHECK(root.scale == Vec3{2.0f, 2.0f, 2.0f});
    REQUIRE(root.mesh.vertices.size() == 3);
    CHECK(root.mesh.vertices[1] == Vec3{11.0f, 0.0f, 0.0f});
    REQUIRE(root.mesh.face_normals.size() == 1);
    // (p2 - p1) x (p2 - p0) = (-1, 1, 0) x (0, 1, 0)
    CHECK(root.mesh.face_normals[0] == Vec3{0.0f, 0.0f, -1.0f});
    CHECK(root.mesh.corner_normals[0][2] == Vec3{0.0f, 0.0f, -1.0f});
    CHECK(root.translation_keys.size() == (minor < 5 ? 1u : 0u));
    CHECK(model.volumes.size() == (minor >= 2 ? 1u : 0u));
    if (minor >= 3) CHECK(model.volumes[0].values[0] == 3.0f);  // flag 0: 3 added
    if (minor == 2) CHECK(model.volumes[0].values[0] == 0.0f);
    if (minor == 1) {
      CHECK(root.mesh.faces[0].smoothing_group == 0);
    } else {
      CHECK(root.mesh.faces[0].smoothing_group == 3);
    }
  }
}

TEST_CASE("the face sort moves all but bytes 0xe..0x13, and is the client's") {
  std::vector<ModelFace> faces;
  faces.reserve(4);
  for (std::uint16_t k = 0; k < 4; ++k) faces.push_back(Face(static_cast<std::uint16_t>(3 - k), k));
  SortModelFaces(faces);
  for (std::uint16_t k = 0; k < 4; ++k) {
    CHECK(faces[k].texture == k);
    CHECK(faces[k].vertices[0] == 3 - k);
    CHECK(faces[k].field_10 == k);  // stayed where it was
  }
  // 40 faces of two textures: not the order a stable sort gives.
  faces.clear();
  faces.reserve(40);
  for (std::uint16_t k = 0; k < 40; ++k)
    faces.push_back(Face(static_cast<std::uint16_t>(k % 2), k));
  SortModelFaces(faces);
  bool stable = true;
  for (std::size_t k = 1; k < faces.size(); ++k) {
    CHECK(faces[k - 1].texture <= faces[k].texture);
    if (faces[k - 1].texture == faces[k].texture && faces[k - 1].vertices[0] > faces[k].vertices[0])
      stable = false;
  }
  CHECK_FALSE(stable);
}

TEST_CASE("a face with no area gets the x87's negative NaN") {
  ModelMesh mesh;
  mesh.vertices = {Vec3{1, 1, 1}, Vec3{1, 1, 1}, Vec3{2, 3, 4}};
  ModelFace face;
  face.vertices = {0, 1, 2};
  mesh.faces = {face};
  REQUIRE(ComputeModelNormals(mesh));
  CHECK(std::bit_cast<std::uint32_t>(mesh.face_normals[0][0]) == 0xffc00000u);
  CHECK(std::isnan(mesh.corner_normals[0][0][0]));
}

TEST_CASE("models the original refuses, cannot link, or reads past") {
  Model model;
  std::string error;
  auto bad = Rsm(4);
  bad[3] = 'X';
  CHECK_FALSE(DecodeModel(bad, model, &error));
  CHECK(error == "a model file of the wrong format");
  CHECK_FALSE(DecodeModel(Rsm(6), model, &error));
  CHECK(error == "an unsupported version; the contents may be broken");
  auto orphan = Rsm(4);
  const auto at =
      std::search(orphan.begin() + 200, orphan.end(), std::begin("root"), std::begin("root") + 4);
  REQUIRE(at != orphan.end());
  (*at) = 'x';  // the arm's parent name: its parent goes missing
  CHECK_FALSE(DecodeModel(orphan, model, &error));
  const auto whole = Rsm(4);
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeModel(std::span(whole.data(), size), model, &error));
  }
}

TEST_CASE("RsmRes loads from model\\ through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_rsm_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "model");
  {
    const auto rsm = Rsm(4);
    std::ofstream f(dir / "data" / "model" / "tree.rsm", std::ios::binary);
    f.write(reinterpret_cast<const char*>(rsm.data()), static_cast<std::streamsize>(rsm.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterModelTypes(manager);
  auto* tree = static_cast<RsmRes*>(manager.Get("Tree.rsm"));
  REQUIRE(tree != nullptr);
  CHECK(tree->model().nodes.size() == 2);
  CHECK(manager.Get("missing.rsm") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
