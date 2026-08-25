const { defineConfig } = require("@playwright/test");

module.exports = defineConfig({
  testDir: ".",
  timeout: 20000,
  workers: 1,
  use: {
    baseURL: process.env.MOCK_URL || "http://localhost:8765",
    headless: true,
    viewport: { width: 1440, height: 1000 },
  },
  webServer: {
    command: "node mock_esp32.js",
    port: 8765,
    reuseExistingServer: true,
    timeout: 8000,
  },
});