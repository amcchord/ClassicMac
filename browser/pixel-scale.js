// Picks a scale that fills as much of the browser viewport as possible while
// keeping enlarged guest pixels on whole-number boundaries. When the guest
// must shrink, prefer the closest exact-aspect CSS pixel dimensions so the
// browser's nearest-neighbor sampler has stable edges.
export function fitPixelScale(
  framebufferWidth,
  framebufferHeight,
  containerWidth,
  containerHeight
) {
  if (
    framebufferWidth <= 0 ||
    framebufferHeight <= 0 ||
    containerWidth <= 0 ||
    containerHeight <= 0
  ) {
    return 0;
  }

  const fit = Math.min(
    containerWidth / framebufferWidth,
    containerHeight / framebufferHeight
  );

  if (fit >= 1) {
    return Math.max(1, Math.floor(fit));
  }

  const divisor = greatestCommonDivisor(framebufferWidth, framebufferHeight);
  const aspectUnitWidth = framebufferWidth / divisor;
  const aspectUnitHeight = framebufferHeight / divisor;
  const aspectUnits = Math.floor(
    Math.min(
      containerWidth / aspectUnitWidth,
      containerHeight / aspectUnitHeight
    )
  );

  return aspectUnits >= 1 ? aspectUnits / divisor : fit;
}

function greatestCommonDivisor(left, right) {
  let a = Math.abs(Math.trunc(left));
  let b = Math.abs(Math.trunc(right));
  while (b !== 0) {
    [a, b] = [b, a % b];
  }
  return a || 1;
}
