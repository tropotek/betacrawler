// Whether this browser can drive a board, and what to tell the user when it
// cannot. Feature detection decides the verdict; the user agent only picks the
// wording, so a browser this file has never heard of is still judged on what it
// actually exposes rather than on its name.
//
// `level` is 'ok' (say nothing), 'warn' (the app works, flashing does not) or
// 'block' (no device access at all).

const CHROMIUM = 'Chrome, Edge, Brave or Opera';

// Platform before engine: no browser on iOS or Android has Web Serial, whatever
// it calls itself, and saying "use Chrome" to someone already in Chrome for
// Android is the least useful answer available.
function familyOf(userAgent) {
  if (/iPhone|iPad|iPod|CriOS|FxiOS|EdgiOS/.test(userAgent)) return 'ios';
  if (/Android/.test(userAgent)) return 'android';
  if (/Firefox\//.test(userAgent)) return 'firefox';
  if (/Edg\//.test(userAgent)) return 'edge';
  if (/OPR\//.test(userAgent)) return 'opera';
  if (/Chrome\//.test(userAgent)) return 'chrome';
  if (/Safari\//.test(userAgent)) return 'safari';
  return 'unknown';
}

const NO_SERIAL = {
  ios: 'Every browser on iPhone and iPad uses Safari\'s engine, which has no Web '
    + `Serial API. Open this page on a computer, in ${CHROMIUM}.`,
  android: 'Web Serial is not available on Android in any browser. Open this page '
    + `on a computer, in ${CHROMIUM}.`,
  firefox: `Firefox has no Web Serial API. Open this page in ${CHROMIUM}.`,
  safari: `Safari has no Web Serial API. Open this page in ${CHROMIUM}.`,
  // A Chromium browser with the API missing is not the usual "wrong browser"
  // case, so it does not get the usual advice: something has turned it off.
  chromium: 'This is a Chromium browser, but it is not exposing the Web Serial '
    + 'API — it is most likely disabled by enterprise policy or by a flag. Check '
    + 'chrome://flags and, on a managed machine, with whoever administers it.',
  unknown: `This browser has no Web Serial API. Open this page in ${CHROMIUM}.`,
};

export function assessBrowser({
  userAgent = '', hasSerial = false, hasUsb = false, isSecureContext = true,
} = {}) {
  const browser = familyOf(userAgent);

  // Checked before the API itself, because an insecure context hides Web Serial
  // in Chrome too -- and telling a Chrome user to install Chrome is a dead end.
  if (!isSecureContext) {
    return {
      ok: false, level: 'block', browser,
      title: 'This page needs a secure connection',
      message: 'Browsers only expose Web Serial and WebUSB in a secure context, so '
        + 'they are hidden here even in a supported browser. Open this page over '
        + 'https://, or from http://localhost.',
    };
  }

  if (!hasSerial) {
    const chromium = browser === 'chrome' || browser === 'edge' || browser === 'opera';
    return {
      ok: false, level: 'block', browser,
      title: 'This browser cannot talk to your board',
      message: NO_SERIAL[chromium ? 'chromium' : browser],
    };
  }

  // Both APIs ship together in Chromium, so this is the same "switched off
  // somewhere" case as above -- but only flashing is lost, and the rest of the
  // app is worth letting the user get on with.
  if (!hasUsb) {
    return {
      ok: false, level: 'warn', browser,
      title: 'Firmware flashing is unavailable',
      message: 'This browser has Web Serial but not WebUSB, so it can configure a '
        + 'board but cannot flash one. Flashing needs WebUSB — check chrome://flags, '
        + `or open this page in ${CHROMIUM}.`,
    };
  }

  return { ok: true, level: 'ok', browser, title: '', message: '' };
}
