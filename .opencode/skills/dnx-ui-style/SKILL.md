---
name: dnx-ui-style
description: Aplica el sistema de diseño DNX UI Style al crear o modificar ventanas/controles WPF o WinUI (Settings Cards, paleta Fluent Mica/Acrylic, tipografía Segoe UI, navegación sidebar, controles DNX). Use when creating XAML, styles, resources, windows, dialogs, or UI controls; when the user mentions DNX, DNXStyle, "estilo DNX", Fluent, Mica, Acrylic, Settings Card, or theme Light/Dark.
---

# DNX UI Style - Design System

> **Design Name:** DNX UI Style
> **Philosophy:** Fluent Design System (Clean, Modern, Depth, Mica/Acrylic)
> **Core Pattern:** "Settings Card" (Grouped controls in clean cards)
> **Usage:** Project-Agnostic (Applicable to any WPF/WinUI project)

## 1. Color Palette (Theming)

### A. Dark Theme (Default)
Based on Windows 11 "Dark Mica" aesthetic.

| Element | Color Hex | Description |
| :--- | :--- | :--- |
| **Window Background** | `#202020` | Main application background (or Mica effect). |
| **Card Background** | `#2B2B2B` | Settings card background. |
| **Card Border** | `#383838` | Subtle border to separate cards. |
| **Text Primary** | `#FFFFFF` | Headings and values (Opacity 1.0). |
| **Text Secondary** | `#A0A0A0` | Descriptions and subtitles (Opacity ~0.7). |
| **Control Fill** | `#3C3C3C` | Input backgrounds, inactive states. |
| **Control Stroke** | `#505050` | Borders for inputs/buttons. |

### B. Light Theme
Based on Windows 11 "Light Mica" aesthetic.

| Element | Color Hex | Description |
| :--- | :--- | :--- |
| **Window Background** | `#F3F3F3` | Main application background (Mica Alt). |
| **Card Background** | `#FFFFFF` | Settings card background (Clean White). |
| **Card Border** | `#E5E5E5` | Very subtle border. |
| **Text Primary** | `#1A1A1A` | Headings and values (Almost Black). |
| **Text Secondary** | `#5D5D5D` | Descriptions and subtitles. |
| **Control Fill** | `#F9F9F9` | Input backgrounds. |
| **Control Stroke** | `#D1D1D1` | Borders for inputs/buttons. |

### C. Shared Colors (Accent & Status)
| Element | Color Hex | Description |
| :--- | :--- | :--- |
| **Accent Color** | `#0078D4` | Default System Blue (Customizable per project). |
| **Danger/Error** | `#E81123` | Critical actions or errors. |
| **Success/On** | `#107C10` | Positive states. |
| **Warning** | `#FFB900` | Alerts or cautions. |

---

## 2. Typography (Segoe UI Variable)

| Level | Size | Weight | Usage |
| :--- | :--- | :--- | :--- |
| **Page Header** | `24px` | SemiBold | Section titles (e.g., "Home", "Settings"). |
| **Card Title** | `14px` | SemiBold | Setting name within a card. |
| **Card Description** | `12px` | Regular | Explanatory text below title. |
| **Body/Value** | `14px` | Regular | Text inside controls or values. |

---

## 3. Control Catalog (Settings Card Pattern)

The fundamental building block is the **"Settings Card"**.

### A. Card Structure
A container with rounded corners grouping a single configuration item.
- **CornerRadius:** `4px` or `8px` (Consistent within project).
- **Padding:** `16px` (Horizontal), `12px` (Vertical).
- **BorderThickness:** `1px`.

### B. Card Variants

#### 1. Toggle Card
- **Left:** Icon + Title + Description.
- **Right:** `ToggleSwitch`.
- **Usage:** On/Off features.

#### 2. Action Card
- **Left:** Icon + Title + Description.
- **Right:** `Button` (Icon or Text) or `HyperlinkButton`.
- **Usage:** Navigation, Commands, Resets.
- **Style:** Transparent/Outline button to reduce visual noise.

#### 3. Select Card
- **Left:** Icon + Title.
- **Right:** `ComboBox`.
- **Usage:** Mode selection, Enumerations.

#### 4. Slider Card
- **Layout:** Vertical stack or Horizontal expanded.
- **Top:** Title + Current Value (Right aligned).
- **Bottom:** `Slider` (Full width).
- **Usage:** Numeric adjustments (0-100, Scale factors).

#### 5. ComboBox (Dropdown)
- **Height:** `45px`.
- **Background:** `#454545` (Dark Gray).
- **CornerRadius:** `6px`.
- **Typography:** `16px` White.
- **Arrow:** Simple Down Chevron (`\xE70D`).
- **Popup:** Matches background, slide animation.

---

## 4. Custom Controls (DNX Specials)

### A. "Kitt" Scanner
- **Concept:** Oscillating light bar (Knight Rider style).
- **Visuals:**
    - Container: Dark elongated rectangle.
    - Light: Horizontal gradient moving back and forth.
    - Effect: `DropShadow` (Glow) matching Accent Color.

### B. "LED" Indicators
- **Concept:** Status indicators.
- **Shape:** Circular (`Ellipse`) or Rounded Square (`Rectangle`).
- **States:**
    - **OFF:** Stroke (Accent/Gray), Fill (Transparent/Dark).
    - **ON:** Fill (Accent Bright) + Glow Effect.

---

## 5. Layout Guidelines

- **Navigation:** Sidebar (Hamburger) style.
    - **Width:** Fixed (e.g., 250px) or Compact/Expanded modes.
    - **Background:** Transparent (uses Window Background).
    - **Item Height:** `40px` - `48px`.
    - **Typography:**
        - Header: `20px Bold` (e.g., "AMD HANDHELD CONTROL").
        - Item: `16px` (Segoe Fluent Icons + Text).
    - **States:**
        - **Normal:** Text `#AAAAAA`, Background Transparent.
        - **Hover:** Text `#FFFFFF`, Background `#1AFFFFFF` (White 10%).
        - **Selected:** Text `#FFFFFF`, Background `AccentColor`.
    - **Submenus (Accordion):**
        - Indented items.
        - Parent item toggles visibility.
        - Chevron indicator (`\xE70D` Down, `\xE70E` Up) rotates/changes.
- **Content Area:**
    - Padding: `24px`.
    - Spacing: `8px` - `12px` between cards.
    - MaxWidth: Optional constraint for readability on wide screens.

---

## 6. Implementation Notes
- Use **ModernWPF** or **WinUI 3** libraries for base controls.
- Support dynamic theme switching (Light/Dark) at runtime.
- Icons: **Segoe Fluent Icons** (Monoline style).

## 7. Standard Dimensions & Controls
- **Buttons:**
    - Standard Height: `40px`.
    - BorderRadius: `4px`.
- **Typography:**
    - List Items / Standard Labels: `16px`.
    - Secondary/Meta Text: `12px`.
    - Section Headers: `20px` - `24px`.
- **Toggles vs Checkboxes:**
    - **Preferred:** `ToggleSwitch` (Text on Left, Switch on Right).
    - **Avoid:** Standard `CheckBox` for settings.
    - **Layout:** Use a `Grid` or `DockPanel`.

## 8. Checklist al implementar UI
- [ ] Usar Settings Cards para agrupar cada opción (contenedor redondeado 4-8px, borde 1px, padding 16x12px).
- [ ] Respetar la paleta del tema activo (Dark `#202020` / Light `#F3F3F3`) usando recursos, no colores en duro.
- [ ] Tipografía Segoe UI Variable con los tamaños de la sección 2.
- [ ] `ToggleSwitch` en vez de `CheckBox` para opciones on/off.
- [ ] Botones de 40px de alto y 4px de radio.
- [ ] Soporte de cambio de tema Light/Dark en runtime.
- [ ] Iconos Segoe Fluent Icons (monoline).
