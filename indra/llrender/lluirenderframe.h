/** Backend-neutral, owned output of one real UI traversal. */
#ifndef LL_LLUIRENDERFRAME_H
#define LL_LLUIRENDERFRAME_H

#include <array>
#include <cstdint>
#include <vector>

namespace LLUIRender
{
struct Vertex
{
    float x, y, u, v;
    std::array<std::uint8_t, 4> color;
};

struct Image
{
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Draw
{
    std::uint32_t first = 0, count = 0, image = 0;
    // Pixel coordinates, top-left origin, already intersected with the drawable.
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    bool alphaMask = false;
};

struct Frame
{
    std::uint32_t width = 0, height = 0;
    std::vector<Vertex> vertices;
    std::vector<Image> images;
    std::vector<Draw> draws;
    bool valid() const noexcept;
};
}
#endif
