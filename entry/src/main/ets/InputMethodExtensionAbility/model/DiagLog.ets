/**
 * DiagLog.ets
 * Диагностический логгер — держит в памяти + пишет в файл.
 */

import fs from '@ohos.file.fs';
import { buffer } from '@kit.ArkTS';
import type { Context } from '@kit.AbilityKit';

const MAX_ENTRIES = 100;

class DiagLog {
  private entries: string[] = [];
  private logPath: string = '';
  private ready: boolean = false;

  init(context: Context): void {
    this.logPath = context.filesDir + '/hoskey_diag.log';
    this.ready = true;
    this.add('DiagLog initialized');
  }

  add(msg: string): void {
    const ts = new Date().toISOString().slice(11, 23);
    const line = ts + ' ' + msg;
    this.entries.push(line);
    if (this.entries.length > MAX_ENTRIES) {
      this.entries.shift();
    }
    console.info('[HOSKEY] ' + msg);
    this.appendToFile(line);
  }

  getAll(): string[] {
    return this.entries.slice();
  }

  getText(): string {
    return this.entries.join('\n');
  }

  getLogPath(): string {
    return this.logPath;
  }

  private appendToFile(line: string): void {
    if (!this.ready) return;
    try {
      const file = fs.openSync(this.logPath, fs.OpenMode.CREATE | fs.OpenMode.APPEND | fs.OpenMode.WRITE_ONLY);
      const buf = buffer.from(line + '\n', 'utf-8');
      fs.writeSync(file.fd, buf.buffer);
      fs.closeSync(file);
    } catch (e) {
      // silent
    }
  }
}

const diagLog = new DiagLog();
export default diagLog;
