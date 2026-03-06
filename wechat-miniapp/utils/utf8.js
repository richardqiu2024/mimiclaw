function encodeUtf8(str) {
  if (typeof TextEncoder !== "undefined") {
    return new TextEncoder().encode(str);
  }

  const encoded = unescape(encodeURIComponent(str));
  const bytes = new Uint8Array(encoded.length);
  for (let i = 0; i < encoded.length; i++) {
    bytes[i] = encoded.charCodeAt(i);
  }
  return bytes;
}

function decodeUtf8(buffer) {
  if (!buffer) return "";
  const bytes = buffer instanceof ArrayBuffer ? new Uint8Array(buffer) : buffer;

  if (typeof TextDecoder !== "undefined") {
    return new TextDecoder("utf-8").decode(bytes);
  }

  let binary = "";
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return decodeURIComponent(escape(binary));
}

module.exports = {
  encodeUtf8,
  decodeUtf8,
};
