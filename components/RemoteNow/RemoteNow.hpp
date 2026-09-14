#pragma once

#include "esp_brookesia.hpp"
#include "esp_log.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

	class RemoteNow: public systems::phone::App {
	 public:
		static RemoteNow *requestInstance(
				bool use_status_bar = true, bool use_navigation_bar = false);
		~RemoteNow();

		bool run() override;
		bool back() override;
		bool close() override;

	 protected:
		RemoteNow(bool use_status_bar, bool use_navigation_bar);

	 private:
		static RemoteNow *_instance;
	};

}	 // namespace esp_brookesia::apps
