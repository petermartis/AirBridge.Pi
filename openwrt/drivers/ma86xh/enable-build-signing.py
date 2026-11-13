from pathlib import Path

path = Path("/build/include/package-pack.mk")
text = path.read_text()
old = "$(FAKEROOT) $(STAGING_DIR_HOST)/bin/apk mkpkg \\\n"
new = (
    old
    + '\t  $(if $(filter kmod-ma86xh-rtw89,$(1)),--sign-key /build/ma86xh-signing/private.pem) \\\n'
)
if new not in text:
    assert text.count(old) == 1
    path.write_text(text.replace(old, new))
