size_t
strlcat(char *dst, const char *src, size_t dstsize)
{
  size_t dlen = strnlen(dst, dstsize);
  size_t slen = strlen(src);
  
  if (dlen == dstsize) {
    return dstsize + slen;
  }
  
  size_t copylen = dstsize - dlen - 1;
  if (copylen > 0) {
    strncpy(dst + dlen, src, copylen);
    dst[dlen + copylen] = '\0';
  }
  
  return dlen + slen;
}


size_t
strlcpy(char *dst, const char *src, size_t dstsize) {
  size_t srclen = strlen(src);
  
  if (dstsize > 0) {
    size_t copylen = (srclen >= dstsize) ? dstsize - 1 : srclen;
    memcpy(dst, src, copylen);
    dst[copylen] = '\0';
  }
  
  return srclen;
}
