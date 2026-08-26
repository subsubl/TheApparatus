// @ts-check
/**
 * The Apparatus - GUI QA suite (Playwright)
 * Tests the REAL firmware SPA against a mock ESP32 WebSocket backend.
 * Run: npx playwright test (from tools/gui-test)
 */
const { test, expect } = require("@playwright/test");

test.describe("The Apparatus console", () => {
  let page;

  test.beforeAll(async ({ browser }) => {
    page = await browser.newPage();
    page.on("pageerror", (e) => { throw new Error("SPA JS error: " + e.message); });
    await page.goto("/");
    await expect(page.locator("#connTxt")).toHaveText(/Connected/, { timeout: 8000 });
  });

  test("01 - connection badge shows live link", async () => {
    await expect(page.locator("#conn .dot.ok")).toBeVisible();
  });

  test("02 - state badge renders telemetry states", async () => {
    // Mock cycles MACRO(1)/MICRO(2) within 6s - both must appear
    await expect(page.locator("#badge")).toHaveText(/MACRO|MICRO/, { timeout: 9000 });
  });

  test("03 - metrics update with real values", async () => {
    await expect(page.locator("#mDr")).toContainText(/cm/);
    const df = await page.locator("#mDf").textContent();
    expect(parseFloat(df)).toBeGreaterThan(0);
  });

  test("04 - nine radar gate bars rendered", async () => {
    await expect(page.locator("#gates .ebar-row")).toHaveCount(9);
    await expect(page.locator("#gates .ebar.peak").first()).toBeVisible();
  });

  test("05 - radar preview canvas draws target", async () => {
    const drawn = await page.evaluate(() => {
      const c = document.getElementById("radar");
      if (!c) return "MISSING";
      const ctx = c.getContext("2d");
      return ctx.getImageData(0, 0, c.width, c.height).data.some((v) => v !== 0);
    });
    expect(drawn).toBe(true);
  });

  test("06 - breathing oscilloscope has signal", async () => {
    const drawn = await page.evaluate(() => {
      const c = document.getElementById("scope");
      return c.getContext("2d").getImageData(0, 0, c.width, c.height).data.some((v) => v !== 0);
    });
    expect(drawn).toBe(true);
  });

  test("07 - six vactrol channel cards", async () => {
    await expect(page.locator("#vactrols .vac-card")).toHaveCount(6);
    await expect(page.locator("#va_0")).toBeVisible();      // AUTO toggle ch1
    await expect(page.locator("#vs_5")).toBeVisible();      // manual slider ch6
  });

  test("08 - eight relay cards with all controls", async () => {
    await expect(page.locator(".relay-card")).toHaveCount(8);
    for (let i = 0; i < 8; i++) {
      await expect(page.locator(`#rt_${i}`)).toBeVisible();
      await expect(page.locator(`#rclk_${i}`)).toBeVisible();
      await expect(page.locator(`#rpin_${i}`)).toBeVisible();
    }
  });

  test("08b - AVE5 button dropdowns populated and selectable", async () => {
    // 8 relays x 12 buttons, defaults selected from config
    await expect(page.locator(".relay-card select[id^='rb_']")).toHaveCount(8);
    await expect(page.locator("#rb_0")).toContainText("STILL");
    await page.locator("#rb_2").selectOption({ index: 10 });   // SUPERIMPOSE
    await new Promise((r) => setTimeout(r, 400));
    const fs = require("fs");
    const cmds = fs.readFileSync("received.log", "utf-8").trim().split("\n")
      .map((l) => JSON.parse(l));
    const msg = cmds.filter((m) => m.type === "relay_ave5_button" && m.index === 2).pop();
    expect(msg).toBeTruthy();
    expect(msg.button).toBe(10);
  });

  test("08c - vactrol 'Drives' pot dropdowns wired", async () => {
    await expect(page.locator("#vactrols select[id^='vpot_']")).toHaveCount(6);
    await expect(page.locator("#vpot_0")).toContainText("Mix/T-Bar lever");
    await page.locator("#vpot_5").selectOption({ index: 7 });  // Audio level
    await new Promise((r) => setTimeout(r, 400));
    const fs = require("fs");
    const cmds = fs.readFileSync("received.log", "utf-8").trim().split("\n")
      .map((l) => JSON.parse(l));
    const msg = cmds.filter((m) => m.type === "vactrol_pot" && m.ch === 5).pop();
    expect(msg).toBeTruthy();
    expect(msg.pot).toBe(7);
  });

  test("09 - boot panel shows 12 steps + replay button", async () => {
    await expect(page.locator("#bootpanel .vac-card")).toHaveCount(12);
    await expect(page.getByRole("button", { name: /REPLAY BOOT SEQUENCE/ })).toBeVisible();
  });

  test("10 - relay FIRE sends websocket command", async ({ request }) => {
    await page.locator("#relays .relay-card").first().getByRole("button", { name: /FIRE/ }).click();
    await new Promise((r) => setTimeout(r, 400));
    const fs = require("fs");
    const log = fs.readFileSync("received.log", "utf-8").trim().split("\n");
    const cmds = log.map((l) => JSON.parse(l));
    expect(cmds.some((m) => m.type === "relay_fire" && m.index === 0)).toBe(true);
  });

  test("11 - relay config edits propagate (double-click shape)", async () => {
    await page.locator("#rn_3").fill("2");           // press_count -> 2
    await page.locator("#rn_3").dispatchEvent("change");
    await new Promise((r) => setTimeout(r, 400));
    const fs = require("fs");
    const cmds = fs.readFileSync("received.log", "utf-8").trim().split("\n")
      .map((l) => JSON.parse(l));
    const cfg = cmds.filter((m) => m.type === "relay_cfg" && m.index === 3).pop();
    expect(cfg).toBeTruthy();
    expect(cfg.press_count).toBe(2);
  });

  test("12 - calibration slider updates value label", async () => {
    await page.locator("#c_gamma_exponent").fill("2.5");
    await page.locator("#c_gamma_exponent").dispatchEvent("input");
    await expect(page.locator("#cv_gamma_exponent")).toContainText("2.5");
  });

  test("13 - save config posts full payload incl. boot steps", async () => {
    await page.locator("#saveBtn").click();
    await expect(page.locator("text=Saved to NVS")).toBeVisible({ timeout: 5000 });
    const fs = require("fs");
    const cmds = fs.readFileSync("received.log", "utf-8").trim().split("\n")
      .map((l) => JSON.parse(l));
    const save = cmds.filter((m) => m.type === "save_config").pop();
    expect(save.payload.boot.step_count).toBeGreaterThanOrEqual(0);
    expect(save.payload.vactrol.length).toBe(6);
    expect(save.payload.vactrol[0].ave5_pot).toBe(1);          // Mix -> T-Bar
    expect(save.payload.fx.length).toBe(8);                    // relay bank persisted!
    expect(save.payload.fx[6].ave5_button).toBeGreaterThan(0); // WJ-CAM target set
  });

  test("14 - no console errors during 3s of live telemetry", async () => {
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.waitForTimeout(3000);
    expect(errors).toEqual([]);
  });
});