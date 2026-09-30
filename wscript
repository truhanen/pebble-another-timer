#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    patch_clay_for_new_platforms(ctx)
    ctx.load('pebble_sdk')


def build(ctx):
    patch_clay_for_new_platforms(ctx)
    compile_typescript(ctx)
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        defines = ['USE_FAKE_TIME'] if os.environ.get('FAKE_TIME') else []
        if os.environ.get('SCREENSHOT_FIXTURES'):
            defines.append('SCREENSHOT_FIXTURES')  # seed demo data for appstore screenshots
        if os.environ.get('APP_TEST_HOOKS'):
            defines.append('APP_TEST_HOOKS')  # compiles in test-only AppMessage hooks, see main.c
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app', defines=defines)

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')


def compile_typescript(ctx):
    """Compile the phone-side TypeScript (`src/ts/*.ts`) to CommonJS JS in
    `src/pkjs/` *before* the SDK bundles `src/pkjs/**/*.js`.

    The PKJS source lives in `src/ts/` as modern TypeScript; tsc (pinned to 6.x
    in devDependencies, config in `tsconfig.json`) emits ES5/CommonJS 1:1 into
    `src/pkjs/`, which the Pebble SDK's webpack pass then bundles exactly as it
    would hand-written JS. The generated `src/pkjs/*.js` are gitignored — this
    hook regenerates them on every build, so a clean checkout still bundles
    correctly. A type error aborts the build (tsc has noEmitOnError).
    """
    import subprocess, os
    root = ctx.path.abspath()
    if not os.path.isdir(os.path.join(root, 'src', 'ts')):
        return
    tsc = os.path.join(root, 'node_modules', '.bin', 'tsc')
    cmd = [tsc] if os.path.exists(tsc) else ['npx', 'tsc']
    from waflib import Logs
    Logs.pprint('CYAN', 'Compiling TypeScript (src/ts -> src/pkjs)')
    try:
        subprocess.run(cmd, cwd=root, check=True)
    except subprocess.CalledProcessError:
        ctx.fatal('TypeScript compilation failed')


def patch_clay_for_new_platforms(ctx):
    """Teach pebble-clay 1.0.4 about the newer `flint`/`gabbro` boards.

    Clay ships its compiled C library (`libpebble-clay.a`) only for
    aplite/basalt/chalk/diorite/emery. Because Clay is consumed as a Pebble
    *package*, waf links that C lib into every target platform, so a build that
    targets flint/gabbro fails ("doesn't support the platform") even though we
    use Clay purely for its JS config page and never call a single Clay C
    function. We make the lib link on the new boards by adding flint/gabbro
    copies of an existing board's `.a` (diorite->flint, chalk->gabbro) and the
    matching include stubs. The bytes are never executed (no Clay C calls), so
    using another board's lib only needs to satisfy the linker.

    This patches the installed package in node_modules so it survives a
    `pebble package install` / npm reinstall (which restores the upstream files).
    It is idempotent and a no-op once the entries exist. dist.zip is the source
    of truth: the build re-extracts it each time, so patching the extracted
    dist/ tree would not stick — we patch the zip.
    """
    import json, zipfile, os
    clay = ctx.path.find_node('node_modules/pebble-clay')
    if clay is None:
        return
    clay_dir = clay.abspath()
    new_platforms = {'flint': 'diorite', 'gabbro': 'chalk'}

    # 1) package.json targetPlatforms (the dependency-resolution gate).
    pj_path = os.path.join(clay_dir, 'package.json')
    pj = json.load(open(pj_path))
    tps = pj.get('pebble', {}).get('targetPlatforms', [])
    if tps and any(p not in tps for p in new_platforms):
        for p in new_platforms:
            if p not in tps:
                tps.append(p)
        json.dump(pj, open(pj_path, 'w'), indent=2)

    # 2) dist.zip binaries + include stubs (what the linker/compiler consume).
    zip_path = os.path.join(clay_dir, 'dist.zip')
    if not os.path.exists(zip_path):
        return
    zin = zipfile.ZipFile(zip_path, 'r')
    data = {n: zin.read(n) for n in zin.namelist()}
    zin.close()
    changed = False
    for new, src in new_platforms.items():
        bin_dst = 'binaries/%s/libpebble-clay.a' % new
        bin_src = 'binaries/%s/libpebble-clay.a' % src
        inc_dst = 'include/pebble-clay/%s/src/resource_ids.auto.h' % new
        inc_src = 'include/pebble-clay/%s/src/resource_ids.auto.h' % src
        if bin_dst not in data and bin_src in data:
            data[bin_dst] = data[bin_src]
            changed = True
        if inc_dst not in data and inc_src in data:
            data[inc_dst] = data[inc_src]
            changed = True
    if changed:
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zout:
            for n, b in data.items():
                zout.writestr(n, b)
        from waflib import Logs
        Logs.pprint('CYAN', 'Patched pebble-clay for flint/gabbro')
