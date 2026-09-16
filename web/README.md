# Gerty web installer

Serve this directory over HTTPS or localhost for Web Serial support.

## Styles

The page uses Tailwind CSS and Roboto from Google Fonts. To update styles:

```sh
cd web
npm ci
npm run build
```

Use `npm run watch` while editing. Edit utility classes in `index.html` or shared
styles in `tailwind.css`, then commit the generated `style.css` alongside them.
The compiled stylesheet is included so static hosting needs no Node runtime or
Tailwind CDN script. Firmware publication does not need a CSS build.
