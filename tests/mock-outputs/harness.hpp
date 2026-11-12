// SPDX-License-Identifier: GPL-2.0-or-later
//
// Scripted test harness (test-only). Executes a JSON timeline (path in
// MSD_MOCK_SCENARIO) on the UI thread: creates/starts/stops/releases mock
// outputs, simulates network failures, drives the primary stream and Aitum
// destinations, logs checkpoints and takes dock screenshots (MSD_TEST_OUT).
// Refuses to run unless the module is loaded from a workspace "test-env".
#pragma once

#include <obs.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <map>
#include <thread>
#include <vector>

namespace msdmock {

class Harness : public QObject {
	Q_OBJECT

public:
	explicit Harness(QObject *parent = nullptr);
	~Harness() override;

	bool load(const QString &scenarioPath, const QString &outDir);
	void begin();
	void releaseAll();

private:
	struct Mock {
		obs_output_t *output = nullptr;
		obs_encoder_t *venc = nullptr;
		obs_encoder_t *aenc = nullptr;
		obs_service_t *service = nullptr;
	};

	void runStep(const QJsonObject &step);
	void createMock(const QJsonObject &step);
	void releaseMock(const QString &id);
	void startChurn(const QJsonObject &step);
	void callAitum(const char *request, const QString &name);
	void screenshot(const QJsonObject &step);
	void showDock(const QJsonObject &step);
	// 1.0.1 probes (log "PROBE <kind> name=... key=value ..." lines that
	// tools\run-scenario.ps1 evaluates against the step's "expect").
	void probeLayout(const QJsonObject &step);
	void probeDock(const QJsonObject &step);
	void toggleSections(const QJsonObject &step);
	void probeLag(const QJsonObject &step);

	QJsonArray steps_;
	QString outDir_;
	std::map<QString, Mock> mocks_;
	bool released_ = false;
	int churnRemaining_ = 0;
	int churnIndex_ = 0;
	std::vector<std::thread> workers_;
};

} // namespace msdmock
