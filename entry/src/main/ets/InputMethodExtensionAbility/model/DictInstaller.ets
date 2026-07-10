/**
 * DictInstaller.ets
 * Copies dictionary files from rawfile/dict/ to filesDir/dict/
 */

import fs from '@ohos.file.fs';
import type { Context } from '@kit.AbilityKit';

const DICT_DIR = 'dict';
const HOSDICT_FILE = 'ru.hoskey.hosdict';

export class DictInstaller {

  static async install(context: Context): Promise<string> {
    const dictPath = context.filesDir + '/' + DICT_DIR;
    console.info('[DictInstaller] target=' + dictPath);

    // Create directory
    try {
      fs.mkdirSync(dictPath, true);
    } catch (_) {}

    const installedHosdictPath = dictPath + '/' + HOSDICT_FILE;

    // Get file list from rawfile
    const resMgr = context.resourceManager;
    let fileList: string[] = [];
    try {
      fileList = await resMgr.getRawFileList('dict');
      console.info('[DictInstaller] rawfiles: ' + fileList.length);
    } catch (e) {
      console.error('[DictInstaller] getRawFileList FAIL: ' + e);
      return dictPath;
    }

    // Check if installed file matches rawfile size (force re-copy on update)
    let needsCopy = true;
    try {
      const stat = fs.statSync(installedHosdictPath);
      const raw = await resMgr.getRawFileContent('dict/' + HOSDICT_FILE);
      // Up to date iff the installed size matches the packaged raw asset. The
      // previous "> 1 MB" guard forced a full re-copy on every launch for any
      // dictionary legitimately smaller than a megabyte.
      if (stat.size === raw.byteLength) {
        needsCopy = false;
        console.info('[DictInstaller] hosdict up to date, size=' + stat.size + ', skip copy');
      } else {
        console.info('[DictInstaller] hosdict size mismatch: disk=' + stat.size + ' raw=' + raw.byteLength + ', will recopy');
      }
    } catch (_) {
      console.info('[DictInstaller] hosdict missing or check failed, will copy');
    }

    if (!needsCopy) {
      console.info('[DictInstaller] dictionary runtime is container-only (.hosdict), no external words.txt');
      return dictPath;
    }

    const hosdictFiles = fileList.filter((name: string) => name.endsWith('.hosdict'));
    if (hosdictFiles.length === 0) {
      console.error('[DictInstaller] no .hosdict container found in rawfile/dict');
      return dictPath;
    }

    // Copy only the canonical hosdict container.
    let ok = 0;
    let fail = 0;
    for (const name of hosdictFiles) {
      try {
        const raw = await resMgr.getRawFileContent('dict/' + name);
        if (raw.byteLength === 0) {
          console.warn('[DictInstaller] empty rawfile: ' + name);
          continue;
        }
        const dest = dictPath + '/' + name;
        const fd = fs.openSync(dest, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
        fs.writeSync(fd.fd, raw.buffer);
        fs.closeSync(fd);

        // Verify
        const stat = fs.statSync(dest);
        if (stat.size !== raw.byteLength) {
          console.error('[DictInstaller] SIZE MISMATCH ' + name + ': wrote=' + raw.byteLength + ' disk=' + stat.size);
          fail++;
        } else {
          ok++;
        }
      } catch (e) {
        fail++;
        console.error('[DictInstaller] FAIL ' + name + ': ' + e);
      }
    }

    console.info('[DictInstaller] done: ok=' + ok + ' fail=' + fail + ' total=' + hosdictFiles.length);

    // Final verify
    try {
      const stat = fs.statSync(installedHosdictPath);
      console.info('[DictInstaller] VERIFY hosdict: size=' + stat.size);
    } catch (e) {
      console.error('[DictInstaller] VERIFY hosdict: MISSING! ' + e);
    }

    return dictPath;
  }
}
