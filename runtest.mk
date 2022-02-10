OBJ_COLOR   = \033[0;36m
OK_COLOR    = \033[0;32m
ERROR_COLOR = \033[0;31m
NO_COLOR    = \033[m
OK_STRING    = "[OK]"
ERROR_STRING = "[ERROR]"

define run_and_test
$(1); \
RESULT=$$?; \
printf "%15b" ""$(2)"" ; \
if [ $$RESULT -ne 0 ]; then \
printf "%-43b%b" "$(OBJ_COLOR) $@" "$(ERROR_COLOR)$(ERROR_STRING)$(NO_COLOR)\nCommand: $(ERROR_COLOR)$(1)$(NO_COLOR)\n"   ; \
else \
printf "%-43b%b" "$(OBJ_COLOR) $(@F)" "$(OK_COLOR)$(OK_STRING)$(NO_COLOR)\n"   ; \
fi; \
exit $$RESULT
endef

