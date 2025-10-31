import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    viteSingleFile()
  ],
  build: {
    minify: "terser",
    terserOptions: { compress: { drop_console: true } },
    cssCodeSplit: false,
    assetsInlineLimit: 100000000,
  },
})
