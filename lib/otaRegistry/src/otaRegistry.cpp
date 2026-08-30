#include "otaRegistry.hpp"
#include <string.h>                                                 /// String comparison utilities.
#include <pgmspace.h>                                               /// strcmp_P for the PROGMEM file names.

OtaTarget* OtaRegistry::head = nullptr;

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

void OtaRegistry::triggerForFile(const char* fileName) {
  if(fileName == nullptr) { return; }
  for(OtaTarget* current = head; current != nullptr; current = current->next) {
    const char* targetFile = current->getFwFileName();
    if(targetFile != nullptr && strcmp_P(fileName, targetFile) == 0) {
      current->triggerOta();
    }
  }
}
