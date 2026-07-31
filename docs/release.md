# Release

## Merging does not release anything

`release.yml` triggers on tags only:

```yaml
on:
  push:
    tags:
      - 'v*'
```

Pushing to `main` runs the build, tests and format checks. It does **not** publish.

## Cutting a release

```bash
git checkout main && git pull

# keep the committed version in step with the tag
sed -i '' 's/^Version: .*/Version: 0.2.0/' layout/DEBIAN/control
git commit -am "chore: bump version to 0.2.0"
git push

git tag v0.2.0
git push origin v0.2.0
```

The workflow then:

1. Installs Theos and the iPhoneOS SDK (both cached).
2. **Rewrites `layout/DEBIAN/control`'s `Version:` from the tag name.**
3. Runs `make clean && make package`.
4. Creates a GitHub Release and attaches the `.deb`.

## The tag is the version source of truth

Step 2 means the value committed in `layout/DEBIAN/control` does not decide the
released version — the tag does. The committed value only affects local builds and
dev builds, which is why it is worth keeping in step anyway: otherwise a local
`make package` produces a confusingly-named `.deb`.

## A failed build publishes nothing

The package step runs before the release step, so a broken build fails the workflow
without creating a release.

!!! warning "The tag survives a failed build"
    You have to delete it before retrying:

    ```bash
    git tag -d v0.2.0
    git push origin :refs/tags/v0.2.0
    ```

Check the PR's `Build` check is green **before** tagging. That check is the only
thing that verifies the Theos build, Logos preprocessing of `Tweak.xm` and `.deb`
packaging — none of which `make check` covers.

## Versioning

Loosely semantic, `0.x` while the API is still moving:

- **patch** — bug fixes, no API change
- **minor** — new tabs or capabilities, or a core API change
- **major** — reserved for 1.0

Dev builds are versioned automatically as `BASE-dev.N+sha7` by `build.yml`, so
every PR build is installable and distinguishable.

## Release checklist

- [ ] `make check` passes locally (fmt-check, 527 tests, compile + link)
- [ ] The PR's `Build` check is green — this is the real Theos verification
- [ ] Anything not verified is called out in the release notes
- [ ] `layout/DEBIAN/control` `Version:` matches the tag being cut
- [ ] Installed and smoke-tested on a device, if the change touches runtime
      behaviour

!!! note "What CI cannot tell you"
    A green build proves it compiles, links and packages. It does not prove the
    panel appears, gestures work, or a scan finds anything. Those need a device.
