#include "otaRegistry.hpp"
#include <string.h>                                                 /// String comparison utilities.
#include <pgmspace.h>                                               /// strcmp_P for the PROGMEM file names.

OtaTarget* OtaRegistry::head = nullptr;
OtaImageInfo OtaRegistry::batchImage;
RecursiveMutex OtaRegistry::mutex;

void OtaRegistry::add(OtaTarget& target) {
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    if(current == &target) { return; }
  }
  target.next = nullptr;
  if(head == nullptr) {
    head = &target;
    return;
  }
  OtaTarget* current = head;
  while(current->next != nullptr) {
    current = current->next;
  }
  current->next = &target;
}

void OtaRegistry::queueForFile(const char* fileName) {
  if(fileName == nullptr) { return; }
  const LockGuard guard(mutex);
  batchImage = OtaImageInfo{};                                      // A new file: nothing known about it yet.
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    const char* targetFile = current->getFwFileName();
    current->otaQueued = (targetFile != nullptr) && (strcmp_P(fileName, targetFile) == 0);
  }
}

bool OtaRegistry::claimStart(OtaTarget& target, OtaImageInfo& image) {
  const LockGuard guard(mutex);
  if(!target.otaQueued) { return false; }
  if(anyInProgress()) { return false; }
  target.otaQueued = false;                                         // One turn per queueing, taken or not.
  if(!target.isOtaTargetOnline()) { return false; }
  image = batchImage;
  return true;
}

bool OtaRegistry::isFileInUse(const char* fileName) {
  if(fileName == nullptr) { return false; }
  const LockGuard guard(mutex);
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    const char* targetFile = current->getFwFileName();
    if((targetFile == nullptr) || (strcmp_P(fileName, targetFile) != 0)) { continue; }
    if(current->otaQueued || current->isOtaInProgress()) { return true; }
  }
  return false;
}

void OtaRegistry::reportImage(const OtaImageInfo& image) {
  const LockGuard guard(mutex);
  batchImage = image;
}

bool OtaRegistry::anyInProgress() {
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    if(current->isOtaInProgress()) { return true; }
  }
  return false;
}
