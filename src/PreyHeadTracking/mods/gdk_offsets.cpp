// Every Xbox / Game Pass (GDK) build of Prey this mod knows about. Append-only,
// for the same reason as the Steam file.
//
// The GDK copy is a separate binary, not the Steam one repackaged: it is a 2023
// recompile of the same engine, its exe lives under
// Binaries\Danielle\Gaming.Desktop.x64\Release rather than
// Binaries\Danielle\x64\Release, and it installs to C:\XboxGames\Prey\Content.
//
// Two things the derivation turned up that a reader needs before trusting these
// numbers. The recompile changed register allocation and instruction scheduling
// broadly, so several functions are semantically identical while sharing almost
// no bytes with their Steam counterparts - a byte signature is not evidence here
// and every RVA below was pinned by a name, a string, a CVar registration or a
// position in an unchanged call sequence. And the view camera moved from
// CSystem+0x788 to CSystem+0x778, which reaches nothing in this profile because
// the camera is resolved through GetViewCamera at runtime.

#include "BuildProfile.hpp"

namespace preyht {

// GDK campaign build, TimeDateStamp 0x64679C11 (2023-05-19), package 1.13.5.0.
//
// gEnv->pSystem      = RVA 0x208A9A0. 86 of the 144 indirect calls at ISystem
//                      vtable +0x388 load it, the same distribution the Steam
//                      build shows for its own 0x224DA60, and the same global
//                      appears at the body's camera read below.
// gEnv->bMultiplayer = RVA 0x208AAC1, the byte handed to the bool-returning
//                      helper by the "IsMultiplayer" script bind at RVA
//                      0xCEDEC0. The bind was identified from the script-bind
//                      registration itself, which loads its address and then
//                      the name string two instructions later.
// ISystem vtable     = RVA 0x1CD3118, located by the one place in the image
//                      where `add rcx, N; jmp <copy>` and
//                      `lea rax, [rcx + N]; ret` sit 8 bytes apart in a table -
//                      Set/GetViewCamera. Slots are unchanged: +0x380
//                      SetViewCamera (RVA 0xDACD60), +0x388 GetViewCamera
//                      (RVA 0xDAB370), +0x048 Render (RVA 0xDC4240, which
//                      hands this+0x778 to the 3D engine and still guards the
//                      draw with the three-axis 0.05 translation test).
// CCamera            = matrix at 0, and the SetViewCameraFov script bind at
//                      RVA 0xCF1680 writes m_fov +0x30, width/height
//                      +0x38/+0x3C, m_ProjectionRatio +0x40, pixel aspect
//                      +0x44, near +0x4C (0.1) and far +0x64 (8000) exactly as
//                      the Steam build does.
extern const BuildProfile kGdkProfile_20230519 = {
    "gdk-win64-20230519", { 0x64679C11u, 0x02C51000u, 0x00000000u },
    0x208A9A0u, 0x208AAC1u, 0x048u, 0x388u, 0x380u, 0x0u,
    // CCamera::UpdateFrustum, RVA 0x12E930 - the call the SetViewCameraFov
    // bind makes with the camera in rcx after writing the projection, in the
    // same position as the Steam build's tail call to its own 0x121D70. The
    // two function bodies read the same fields (matrix floats 0x00..0x2C, then
    // 0x54/0x58/0x5C) in a different order, which is why no byte signature
    // carries across.
    0x12E930u,
    0x30u, 0x40u,
    // The reticle-preference handler is line-for-line identical across the two
    // builds and carries four of these fields at once: the game CVar block
    // pointer (RVA 0x2A59780), g_reticleYPercentage at +0x2D0 within it, the
    // "DanielleHUD" element fetch (RVA 0x15E0BD0) and the one-float
    // CallFunction. The two-float CallFunction is RVA 0x112D750, taken from the
    // engine's own per-frame reticle update at RVA 0x152F050, which passes it
    // the "reticlePosition" string.
    0x15E0BD0u, 0x112D750u, 0x2A59780u, 0x2D0u,
    // cl_fov at +0x4 and cl_hfov at +0x8, read off `lea r8, [rdi + N]` in
    // their registrations in the client CVar setup at RVA 0x1690AA0. The
    // change handler that clamps both to 25..120 degrees is RVA 0x169EF00.
    0x4u, 0x8u,
    // r_DrawNearFoV's float storage, RVA 0x295E938 - the address passed as the
    // storage argument by its registration at RVA 0xE821F0, immediately before
    // the name string, with the same 55-degree default as the Steam build.
    0x295E938u,
    // The per-frame HUD projection update, RVA 0x15E2590. It is the sole caller
    // of the marker-array walk (RVA 0x150D590) and reads the view camera
    // through +0x388 on the way in, and its whole call sequence lines up
    // one-to-one with the Steam build's 0x1667440.
    0x15E2590u,
    // The focus resolver called from inside that update, RVA 0x15090C0, in the
    // same position in that sequence as the Steam build's 0x1585320. It reads
    // the view camera to decide what the player is looking at, so it gets the
    // clean camera back rather than the tracked one.
    0x15090C0u,
    // The GetViewCamera call site the first-person body is built from: the
    // return address of the call at RVA 0x94E934, inside CEntityObject::Render
    // below. That function makes four camera reads and only the second one
    // places the body, so the site is pinned by its guard rather than by
    // counting: it sits behind the same `test r12b, r12b` on the node flags
    // byte and the same `cmp qword [this+0x68], 0` null check as the Steam
    // build's 0x97456F, loading gEnv->pSystem in between.
    0x94E93Au,
    // The 3D engine's CVar block, RVA 0x227C4E8, being the global that gets
    // loaded before an access at +0x314 more often than any other - the same
    // test that picks the Steam build's 0x243A688 out - and sitting at the same
    // 0x38 distance from the neighbouring 3D-engine global that the Steam pair
    // does. e_CoverageBuffer is at +0x314 and pl_forceFlashlight at +0x8C4 in
    // the game block, both read off `lea r8, [reg + N]` in their registrations.
    0x227C4E8u, 0x314u, 0x8C4u,
    // C3DEngine::AddDynamicLightSource, RVA 0x21BAC0, reached by walking back
    // from its own "more than %d dynamic light sources created" log string.
    0x21BAC0u,
    // The ArkLight fields are deliberately zero, which leaves HasFlashlight()
    // false and the flashlight path off on this build. They are the only
    // addresses in the Steam profile that were never derived statically - the
    // class carries no RTTI in either binary, so the Steam values came from
    // scanning the live process with a lit flashlight, and repeating that needs
    // the Xbox copy running with a save that has one. Nothing observable is
    // lost: the beam does not follow the head on the Steam build either (the
    // direction is computed from the character attachment and never passes
    // through the entity transform these addresses reach), so this path is
    // diagnostic today.
    0u, 0u,   // arklight_set_rva, arklight_entity_off
    0u,       // arklight_vtable_rva
    // IEntity::SetPosRotScale, RVA 0x8E7CB0 - byte-identical to the Steam
    // build's 0x90AF90. Populated because it is soundly derived, though the
    // zeroed ArkLight fields above mean nothing reaches it yet.
    0x8E7CB0u,
    // gEnv->pPhysicalWorld at RVA 0x208A908: it sits at the same 0x98 below
    // gEnv->pSystem as on the Steam build, and it is the global loaded by the
    // SRWIParams ray call sites - RVA 0x306397 builds the struct with objtypes
    // 7 at +0x30 and flags 0x40F at +0x34 and calls vtable +0x118 through it.
    // Every SRWIParams field offset the mod writes is unchanged.
    0x208A908u, 0x118u,
    // CEntityObject::Render, RVA 0x94D7C0. Confirmed against the Steam build
    // feature for feature rather than by signature: the same `movzx r12d,
    // byte [this+0xAC]` on the node flags, the same `mov rax, [this+0x68]` for
    // the camera-space offset pointer, the same four camera reads in the same
    // order, and the same `call [rax+0xE8]` for the entity's world matrix.
    0x94D7C0u, 0xACu, 0x68u, 0xE8u,
};

}  // namespace preyht
