// Sleep-image preparation adapted from the preserved YACP Web portal.
(() => {
const IMAGE_PREP_DEFAULT_GRAY_THRESHOLDS = [43,128,213];
const IMAGE_PREP_GRAY_THRESHOLD_GAP = 4;
let MAX_WIDTH = 480, MAX_HEIGHT = 800;
function clampImagePrepGrayThresholds(values) {
  const gap = IMAGE_PREP_GRAY_THRESHOLD_GAP;
  const max = 255;
  const thresholds = IMAGE_PREP_DEFAULT_GRAY_THRESHOLDS.map((fallback, index) => {
    const parsed = parseInt(values[index], 10);
    return Number.isFinite(parsed) ? Math.max(0, Math.min(max, parsed)) : fallback;
  });
  thresholds[1] = Math.max(gap, Math.min(max - gap, thresholds[1]));
  thresholds[0] = Math.max(0, Math.min(thresholds[1] - gap, thresholds[0]));
  thresholds[2] = Math.max(thresholds[1] + gap, Math.min(max, thresholds[2]));
  return thresholds;
}


function grayToImagePrepLevel(gray, thresholds) {
  if (gray < thresholds[0]) return 0;
  if (gray < thresholds[1]) return 1;
  if (gray < thresholds[2]) return 2;
  return 3;
}


function resetImagePrepCrop(state, keepCenter) {
  if (!state || !state.loaded) return;
  const targetRatio = MAX_WIDTH / MAX_HEIGHT;
  const srcRatio = state.width / state.height;
  let cropW = state.width;
  let cropH = state.height;
  if (srcRatio > targetRatio) {
    cropW = state.height * targetRatio;
  } else {
    cropH = state.width / targetRatio;
  }
  const zoom = Math.max(35, Math.min(100, state.zoom || 100)) / 100;
  const oldCenterX = state.crop ? state.crop.x + state.crop.w / 2 : state.width / 2;
  const oldCenterY = state.crop ? state.crop.y + state.crop.h / 2 : state.height / 2;
  cropW *= zoom;
  cropH *= zoom;
  const centerX = keepCenter ? oldCenterX : state.width / 2;
  const centerY = keepCenter ? oldCenterY : state.height / 2;
  state.crop = clampImagePrepCrop({
    x: centerX - cropW / 2,
    y: centerY - cropH / 2,
    w: cropW,
    h: cropH,
  }, state);
}


function clampImagePrepCrop(crop, state) {
  const clamped = {
    w: Math.min(Math.max(1, crop.w), state.width),
    h: Math.min(Math.max(1, crop.h), state.height),
    x: crop.x,
    y: crop.y,
  };
  clamped.x = Math.max(0, Math.min(clamped.x, state.width - clamped.w));
  clamped.y = Math.max(0, Math.min(clamped.y, state.height - clamped.h));
  return clamped;
}


function imageDataToGray4Indexes(imageData, width, height, thresholds) {
  const src = imageData.data;
  const lum = new Float32Array(width * height);
  const hist = new Uint32Array(256);
  for (let i = 0, p = 0; i < src.length; i += 4, p++) {
    const alpha = src[i + 3] / 255;
    const r = src[i] * alpha + 255 * (1 - alpha);
    const g = src[i + 1] * alpha + 255 * (1 - alpha);
    const b = src[i + 2] * alpha + 255 * (1 - alpha);
    const y = Math.max(0, Math.min(255, Math.round(r * 0.299 + g * 0.587 + b * 0.114)));
    lum[p] = y;
    hist[y]++;
  }

  const total = width * height;
  const percentile = (target) => {
    let sum = 0;
    for (let i = 0; i < 256; i++) {
      sum += hist[i];
      if (sum >= target) return i;
    }
    return 255;
  };
  const black = percentile(Math.max(1, Math.floor(total * 0.01)));
  const white = percentile(Math.max(1, Math.floor(total * 0.99)));
  if (white - black > 32) {
    for (let i = 0; i < lum.length; i++) {
      lum[i] = Math.max(0, Math.min(255, ((lum[i] - black) * 255) / (white - black)));
    }
  }

  const out = new Uint8Array(width * height);
  const addErr = (x, y, err) => {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const idx = y * width + x;
    lum[idx] = Math.max(0, Math.min(255, lum[idx] + err));
  };

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = y * width + x;
      const oldValue = lum[idx];
      const level = grayToImagePrepLevel(oldValue, thresholds);
      const newValue = level * 85;
      const err = (oldValue - newValue) / 8;
      out[idx] = level;
      addErr(x + 1, y, err);
      addErr(x + 2, y, err);
      addErr(x - 1, y + 1, err);
      addErr(x, y + 1, err);
      addErr(x + 1, y + 1, err);
      addErr(x, y + 2, err);
    }
  }
  return out;
}


function writeBmp2Bit(indexes, width, height) {
  const rowBytes = Math.ceil((width * 2) / 32) * 4;
  const paletteBytes = 16;
  const pixelOffset = 14 + 40 + paletteBytes;
  const pixelBytes = rowBytes * height;
  const buffer = new ArrayBuffer(pixelOffset + pixelBytes);
  const view = new DataView(buffer);
  let o = 0;
  const u16 = (v) => {
    view.setUint16(o, v, true);
    o += 2;
  };
  const u32 = (v) => {
    view.setUint32(o, v, true);
    o += 4;
  };
  const i32 = (v) => {
    view.setInt32(o, v, true);
    o += 4;
  };

  u16(0x4d42);
  u32(buffer.byteLength);
  u16(0);
  u16(0);
  u32(pixelOffset);
  u32(40);
  i32(width);
  i32(-height);
  u16(1);
  u16(2);
  u32(0);
  u32(pixelBytes);
  i32(2835);
  i32(2835);
  u32(4);
  u32(4);
  [0, 85, 170, 255].forEach((v) => {
    view.setUint8(o++, v);
    view.setUint8(o++, v);
    view.setUint8(o++, v);
    view.setUint8(o++, 0);
  });

  const bytes = new Uint8Array(buffer);
  for (let y = 0; y < height; y++) {
    const rowStart = pixelOffset + y * rowBytes;
    for (let x = 0; x < width; x++) {
      const level = indexes[y * width + x] & 0x03;
      bytes[rowStart + (x >> 2)] |= level << (6 - ((x & 3) * 2));
    }
  }
  return new Blob([buffer], { type: "image/bmp" });
}

globalThis.YacpSleepPrep = {
  thresholds: clampImagePrepGrayThresholds, indexes: imageDataToGray4Indexes, bmp: writeBmp2Bit,
  clampCrop: clampImagePrepCrop,
  contain(sourceWidth, sourceHeight, width, height) {
    const scale = Math.min(width / sourceWidth, height / sourceHeight);
    const w = sourceWidth * scale, h = sourceHeight * scale;
    return {x:(width - w) / 2, y:(height - h) / 2, w, h};
  },
  crop(state, width, height, keepCenter = false) {
    MAX_WIDTH = width; MAX_HEIGHT = height; resetImagePrepCrop(state, keepCenter); return state.crop;
  }
};
})();
