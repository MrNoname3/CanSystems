#include "otaRegistry.hpp"
#include <string.h>                                                 /// String comparison utilities.
#include <pgmspace.h>                                               /// strcmp_P for the PROGMEM file names.

OtaTarget* OtaRegistry::head = nullptr;
OtaImageInfo OtaRegistry::batchImage;

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
  batchImage = OtaImageInfo{};                                      // A new file: nothing known about it yet.
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    const char* targetFile = current->getFwFileName();
    current->otaQueued = (targetFile != nullptr) && (strcmp_P(fileName, targetFile) == 0);
  }
  startNext();
}

void OtaRegistry::startNext() {
  if(anyInProgress()) { return; }
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    if(!current->otaQueued) { continue; }
    current->otaQueued = false;
    // A node that is not answering would spend the whole transfer timing out, and hold the
    // queue up while it does.
    if(!current->isOtaTargetOnline()) { continue; }
    current->triggerOta(batchImage);
    // A start that did not take leaves nothing to wait for, so the queue moves on at once.
    if(current->isOtaInProgress()) { return; }
  }
}

bool OtaRegistry::anyInProgress() {
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    if(current->isOtaInProgress()) { return true; }
  }
  return false;
}
