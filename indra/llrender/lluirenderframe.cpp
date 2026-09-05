/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the license only.
 * $/LicenseInfo$
 */

#include "lluirenderframe.h"
#include <cmath>
#include <limits>

bool LLUIRender::Frame::valid() const noexcept
{
    if (!width || !height || width > 16384 || height > 16384 || images.empty() || images.size() > 128 ||
        vertices.empty() || vertices.size() > 1'000'000 || draws.empty()) return false;
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    std::uint64_t imageBytes = 0;
    for (const auto& image : images)
    {
        if (!image.width || !image.height || image.width > 8192 || image.height > 8192 ||
            image.rgba.size() != std::uint64_t(image.width) * image.height * 4) return false;
        imageBytes += image.rgba.size();
        if (imageBytes > 64 * 1024 * 1024) return false;
    }
    for (const auto& vertex : vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) return false;
    }
    std::uint64_t next = 0;
    for (const auto& draw : draws)
    {
        if (!draw.count || draw.count % 3 || draw.first != next ||
            draw.image >= images.size() || draw.x > width || draw.y > height ||
            draw.width > width - draw.x || draw.height > height - draw.y) return false;
        next += draw.count;
        if (next > vertices.size()) return false;
    }
    return next == vertices.size();
}
