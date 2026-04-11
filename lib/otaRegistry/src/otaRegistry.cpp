#include "otaRegistry.hpp"
#include <string.h>                                                 /// String comparison utilities.

OtaTarget* OtaRegistry::head = nullptr;

void OtaRegistry::add(OtaTarget& target) {
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
    if(targetFile != nullptr && strcmp(fileName, targetFile) == 0) {
      current->triggerOta();
    }
  }
}
