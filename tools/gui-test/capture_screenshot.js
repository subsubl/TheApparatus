const { chromium } = require("playwright");
const { spawn } = require("child_process");
const path = require("path");

async function capture() {
  const mockServer = spawn("node", ["mock_esp32.js"], { cwd: __dirname });
  await new Promise((r) => setTimeout(r, 1200));

  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 1400, height: 1600 },
    deviceScaleFactor: 2
  });
  const page = await context.newPage();
  await page.goto("http://localhost:8765/");
  await page.waitForSelector("#connTxt", { timeout: 8000 });
  await page.waitForTimeout(2500);

  const outPath = path.resolve(__dirname, "../../docs/images/webconsole_gui.png");
  await page.screenshot({ path: outPath, fullPage: true });
  console.log("Screenshot saved to", outPath);

  await browser.close();
  mockServer.kill();
  process.exit(0);
}

capture().catch((e) => {
  console.error(e);
  process.exit(1);
});
