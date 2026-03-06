# WeChat Mini-Program UI Redesign Plan

## Goal Description
Redesign the WeChat Mini-Program UI to have a modern, tech-savvy, minimalist, and clear aesthetic, while improving operational simplicity. The current UI feels a bit heavy/retro-cyberpunk with dark backgrounds, gold gradients, and dense information. The redesign will shift towards a "Sleek Modern Dashboard" style (similar to Vercel/Linear), emphasizing extreme clarity, thin borders, high contrast, and refined accent colors.

## User Review Required
> [!NOTE]
> Please review the proposed visual and functional changes before I proceed with modifying the code.
- **Color Palette**: Moving from the retro Gold/Cyan to a sleek Zinc/Black theme with a Neon Cyan/Blue accent for a true modern tech feel.
- **Card Styling**: Utilizing "glassmorphism" (high blur, very low opacity backgrounds) with ultra-thin, low-contrast borders.
- **Component Layout**: Grouping inputs more logically. Softening button colors to reduce visual noise, so that only primary actions stand out.
  
## Proposed Changes

### Global Styles ([app.wxss](file:///c:/Users/richa/Documents/ESPproject/mimiclaw/wechat-miniapp/app.wxss) & [app.json](file:///c:/Users/richa/Documents/ESPproject/mimiclaw/wechat-miniapp/app.json))
- **Variables Update**:
  - `--bg-0`: `#000000` (True Black)
  - `--panel`: `rgba(255, 255, 255, 0.03)`
  - `--border`: `rgba(255, 255, 255, 0.08)`
  - `--ink`: `#FAFAFA`
  - `--ink-subtle`: `#A1A1AA` (Zinc 400)
  - `--brand`: `#00E5FF` (Neon Cyan) or `#3B82F6` (Electric Blue)
- **Typography**: Emphasize `-apple-system, Inter, sans-serif` for clean legibility.
- **Components**:
  - `card`: Flat background with `backdrop-filter`, `1px` solid muted border, minimal shadow, padding adjusted for breathing room.
  - `input`: Seamless inputs, slight background, focused border glow.
  - `button`: Primary buttons get a subtle brand gradient or solid brand color. Secondary buttons use a transparent background with a subtle border and hover/active states.

### Home Page Structure ([pages/index/index.wxml](file:///c:/Users/richa/Documents/ESPproject/mimiclaw/wechat-miniapp/pages/index/index.wxml))
- **Structure Refinement**:
  - Keep the overall card layout but simplify the headers (remove unnecessary "Eyebrow" text if it clutters).
  - Add status dots alongside headers instead of large pills for a more minimalist look.
  - Refactor "Action Clusters" to ensure button sizes are uniform and aligned.
  - Simplify the "Terminal" view to feel like a seamless part of the page rather than a constrained box.

### Home Page Styles ([pages/index/index.wxss](file:///c:/Users/richa/Documents/ESPproject/mimiclaw/wechat-miniapp/pages/index/index.wxss))
- **Background Details**: Replace the heavily visible grid and radial gradients with a very subtle, dark linear gradient or a microscopic grid to keep it clean.
- **Metric Row**: Make metric cards borderless with a very soft fill (`rgba(255,255,255,0.02)`) and clear labels.
- **Device Grid**: Minimalist list items for scanned devices, highlighting RSSI clearly.
- **Terminal**: High contrast monospaced text on a completely black background with a subtle inner shadow, matching modern dev tools.

## Verification Plan
### Automated Tests
- Miniapp structure validation (using wxml/wxss compilation in WeChat Dev Tools).
### Manual Verification
- Open the project in WeChat Developer Tools to visually verify:
  - Dark mode rendering and glassmorphism fallbacks.
  - Layout alignment on different simulated screen sizes (e.g., iPhone X / iPhone 14 Pro).
  - Button states (disabled vs enabled).
