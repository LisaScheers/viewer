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

#include "linden_common.h"
#include "llfontgl.h"
#include "llfontfreetype.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "lluiimage.h"
#include "lltut.h"
#include <limits>

namespace tut
{
struct ui_frame_test
{
    ui_frame_test() { gGL.initUIRecording(); LLFontManager::initClass(); }
    ~ui_frame_test() { gGL.shutdown(); LLFontManager::cleanupClass(); }
};
using ui_frame_group = test_group<ui_frame_test>;
using ui_frame_object = ui_frame_group::object;
ui_frame_group ui_frame_tests("owned UI traversal frame");

template<> template<> void ui_frame_object::test<1>()
{
    gGL.beginUIFrame(640, 480);
    gGL.pushUIMatrix();
    gGL.translateUI(12, 20, 0);
    gl_rect_2d(0, 30, 50, 0, LLColor4::white);
    gGL.popUIMatrix();
    auto frame = gGL.finishUIFrame();
    ensure("real immediate UI draw produced a valid frame", frame.valid());
    ensure_equals("quad tessellation preserved", frame.vertices.size(), std::size_t(6));
    ensure_equals("UI translation preserved", frame.vertices[0].x, 12.f);
    ensure_equals("UI translation preserved", frame.vertices[0].y, 50.f);
    ensure_equals("solid image owned", frame.images[0].rgba.size(), std::size_t(4));
    auto invalid = frame;
    invalid.draws[0].image = 99;
    ensure("bad image rejected", !invalid.valid());
    invalid = frame;
    invalid.draws[0].count++;
    ensure("incomplete triangle rejected", !invalid.valid());
    invalid = frame;
    invalid.vertices[0].x = std::numeric_limits<float>::quiet_NaN();
    ensure("non-finite vertex rejected", !invalid.valid());
}

template<> template<> void ui_frame_object::test<2>()
{
    LLFontGL font;
    ensure("CPU-only head font loads", font.loadFace(LL_UI_TEST_FONT, 16.f, 96.f, 96.f, 400,
           false, 0, EFontHinting::FORCE_AUTOHINT, 0, false));
    gGL.beginUIFrame(640, 480);
    ensure("real glyph traversal renders characters", font.render(LLWString(L"Vulkan progress"), 0, 20.f, 200.f,
           LLColor4::white, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
           100, 600) > 0);
    auto frame = gGL.finishUIFrame();
    ensure("font frame is valid without GL initialization", frame.valid());
    ensure("font atlas is captured", frame.images.size() > 1);
    bool coverage = false;
    for (std::size_t i = 3; i < frame.images[1].rgba.size(); i += 4) coverage |= frame.images[1].rgba[i] != 0;
    ensure("actual glyph alpha exists", coverage);
    font.reset();
    ensure("reset preserves CPU-only rasterization", font.getWidth("Retained") > 0);
    ensure("GL manager never initialized", !gGLManager.mInited);
}

template<> template<> void ui_frame_object::test<3>()
{
    gGL.beginUIFrame(100, 100);
    gGL.setUIClip(-10, 30, 200, 80);
    gl_rect_2d(0, 100, 100, 0, LLColor4::white);
    gGL.resetUIClip();
    gl_rect_2d(0, 20, 20, 0, LLColor4::white);
    auto frame = gGL.finishUIFrame();
    ensure("clip frame valid", frame.valid());
    ensure_equals("two ordered draws", frame.draws.size(), std::size_t(2));
    ensure_equals("clip intersects drawable", frame.draws[0].width, U32(100));
    ensure_equals("clip uses top-left coordinates", frame.draws[0].y, U32(20));
    ensure_equals("clip reset", frame.draws[1].height, U32(100));
}

template<> template<> void ui_frame_object::test<4>()
{
    class Texture final : public LLTexture
    {
    public:
        explicit Texture(LLImageRaw* raw) : mRaw(raw) {}
        LLImageRaw* getRawImage() const override { return mRaw; }
        S32 getWidth(S32 = -1) const override { return mRaw->getWidth(); }
        S32 getHeight(S32 = -1) const override { return mRaw->getHeight(); }
    private:
        LLPointer<LLImageRaw> mRaw;
    };
    LLPointer<LLImageRaw> raw = new LLImageRaw(4, 4, 4);
    raw->clear(20, 40, 60, 128);
    LLUIImagePtr image = new LLUIImage("real nine-slice image", new Texture(raw));
    image->setScaleRegion(LLRectf(.25f, .75f, .75f, .25f));
    gGL.beginUIFrame(640, 480);
    {
        LLGLSUIDefault ui_state;
        image->drawSolid(20, 30, 200, 100, LLColor4::white);
    }
    auto frame = gGL.finishUIFrame();
    ensure("real UIImage traversal is valid without a GL image", frame.valid());
    ensure("nine-slice emits multiple triangles", frame.vertices.size() > 6);
    ensure("solid image draw records alpha-mask semantics", frame.draws.front().alphaMask);
    ensure_equals("image source alpha retained", frame.images[1].rgba[3], U8(128));
    raw->clear(0, 0, 0, 0);
    ensure_equals("completed frame does not alias mutable source pixels", frame.images[1].rgba[3], U8(128));
}

template<> template<> void ui_frame_object::test<5>()
{
    LLPointer<LLFontFreetype> font = new LLFontFreetype;
    ensure("CPU font loads", font->loadFace(LL_UI_TEST_FONT, 16.f, 96.f, 96.f, 400,
           false, 0, EFontHinting::FORCE_AUTOHINT, 0, false));
    const auto* glyph = font->getGlyphInfo('A', EFontGlyphType::Grayscale);
    ensure("CPU glyph exists", glyph != nullptr);
    const auto* cache = font->getFontBitmapCache();
    ensure("CPU atlas exists", cache->getImageRaw(glyph->mBitmapEntry.first, glyph->mBitmapEntry.second) != nullptr);
    ensure("no GL mirror exists", cache->getImageGL(glyph->mBitmapEntry.first, glyph->mBitmapEntry.second) == nullptr);
    font->reset(120.f, 120.f);
    ensure("CPU allocation policy survives reset", !cache->createsGLTextures());
}
}
