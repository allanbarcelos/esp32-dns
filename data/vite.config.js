import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  base: '/esp32/',
  plugins: [react()],
  build: {
    minify: "terser",
    terserOptions: { compress: { drop_console: true } },
  },
})
