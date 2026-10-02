#include "SnapshotStore.h"
#include <cassert>
#include <iostream>
int main(){
  mockMountOK=false;SnapshotStore failed;assert(!failed.begin());assert(!mockFormat);assert(!failed.save("x",2));mockMountOK=true;
  SnapshotStore migration;assert(migration.begin());mockWriteBudget=8;assert(!migration.save("legacy",7));mockWriteBudget=-1;SnapshotStore interruptedMigration;assert(interruptedMigration.begin());char migrationBuffer[7];assert(!interruptedMigration.load(migrationBuffer,sizeof(migrationBuffer)));assert(interruptedMigration.legacyAllowed());mockFiles.clear();
  SnapshotStore first;assert(first.begin());assert(first.legacyAllowed());char original[32]="nama lama",corrected[32]="nama baru",out[32]={};assert(first.save(original,sizeof(original)));assert(first.generation()==1);
  mockWriteBudget=30;assert(!first.save(corrected,sizeof(corrected)));mockWriteBudget=-1;SnapshotStore reboot;assert(reboot.begin());assert(reboot.load(out,sizeof(out)));assert(!strcmp(out,original));
  assert(reboot.save(corrected,sizeof(corrected)));assert(reboot.generation()==2);mockFiles["/brain-b.bin"].back()^=1;SnapshotStore corrupt;assert(corrupt.begin());assert(corrupt.load(out,sizeof(out)));assert(!strcmp(out,original));
  assert(corrupt.scrubRecovery(corrected,sizeof(corrected)));SnapshotStore afterDelete;assert(afterDelete.begin());assert(afterDelete.load(out,sizeof(out)));assert(!strcmp(out,corrected));
  // An old valid snapshot deliberately reintroduced after deletion must be rejected.
  auto sanitizedA=mockFiles["/brain-a.bin"],sanitizedB=mockFiles["/brain-b.bin"];
  Preferences meta;meta.begin("brainmeta");meta.putUInt("floor",0);meta.end();mockFiles.clear();SnapshotStore old;assert(old.begin());assert(old.save(original,sizeof(original)));auto oldFile=mockFiles["/brain-a.bin"];
  mockFiles["/brain-a.bin"]=oldFile;mockFiles["/brain-b.bin"]=sanitizedB;meta.begin("brainmeta");meta.putUInt("floor",2);meta.end();SnapshotStore barrier;assert(barrier.begin());assert(!barrier.legacyAllowed());assert(barrier.load(out,sizeof(out)));assert(!strcmp(out,corrected));mockFiles["/brain-b.bin"].back()^=1;SnapshotStore privacyFail;assert(privacyFail.begin());assert(!privacyFail.load(out,sizeof(out)));assert(!privacyFail.legacyAllowed());
  // Partial deletion still installs a durable generation floor before cleanup.
  mockFiles.clear();meta.begin("brainmeta");meta.clear();meta.end();SnapshotStore cleanup;assert(cleanup.begin());assert(cleanup.save(original,sizeof(original)));mockRemoveOK=false;assert(!cleanup.scrubRecovery(corrected,sizeof(corrected)));mockRemoveOK=true;SnapshotStore cleanupReboot;assert(cleanupReboot.begin());assert(cleanupReboot.load(out,sizeof(out)));assert(!strcmp(out,corrected));
  std::cout<<"PASS: dual snapshots, partial writes, checksum recovery, no format, privacy barrier and interrupted cleanup\n";
}
