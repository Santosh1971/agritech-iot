export const metadata = { title: "Privacy Policy" };

export default function Privacy() {
  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="eyebrow">Privacy Policy</p>
          <h1>NB Agri Automation</h1>
          <p className="lead">Last updated: October 2026</p>
        </div>
      </section>

      <section>
        <div className="wrap stack" style={{ gap: 28, maxWidth: 760 }}>
          <p>
            NB Agri Automation (&ldquo;the App&rdquo;) is published by Agri Sensors and Controls
            (&ldquo;we&rdquo;, &ldquo;us&rdquo;). This policy explains what information the App
            accesses and how it is used.
          </p>

          <div>
            <h3>What the App does</h3>
            <p className="muted">
              The App connects to your own NB Agri smart water controller device to let you
              monitor and control irrigation &mdash; turning your pump on or off, running
              scheduled watering cycles, and viewing water-delivery history.
            </p>
          </div>

          <div>
            <h3>Data we collect</h3>
            <ul className="ticks">
              <li>
                <b>Device connection information</b> &mdash; the App stores your device&rsquo;s
                identifier and connection details locally on your phone so it can reconnect to
                your device automatically. This never leaves your phone except to talk directly
                to your own device or your own configured MQTT broker.
              </li>
              <li>
                <b>Irrigation history</b> &mdash; cycle logs, water volumes, and timestamps are
                read from your device and displayed in the App. This data lives on your device
                and is not collected or stored by us on any server we operate.
              </li>
            </ul>
            <p className="muted">
              We do not collect your name, email, phone number, location history, or any other
              personal information. The App does not require account creation or login. We do
              not use analytics, advertising, or crash-reporting services. No data is sold or
              shared with third parties.
            </p>
          </div>

          <div>
            <h3>Permissions the App requests</h3>
            <ul className="ticks">
              <li>
                <b>WiFi access</b> &mdash; to connect to your device directly over your local
                network when you&rsquo;re nearby, and to help you find and set up your
                device&rsquo;s WiFi during initial setup.
              </li>
              <li>
                <b>Location</b> &mdash; on Android, scanning for nearby WiFi networks requires
                the system&rsquo;s location permission, even though the App does not use, store,
                or transmit your actual location. This permission is used solely to let the WiFi
                network scan function, as required by Android.
              </li>
              <li>
                <b>Network/Internet access</b> &mdash; to reach your device over the cloud (via
                MQTT) when you&rsquo;re not on the same local network, so you can monitor and
                control it from anywhere.
              </li>
            </ul>
          </div>

          <div>
            <h3>Data transmission</h3>
            <p className="muted">
              Communication with your device happens either directly over your local WiFi
              network or via an encrypted connection to the MQTT broker configured for your
              device. We do not operate a separate server that stores your irrigation data.
            </p>
          </div>

          <div>
            <h3>Children&rsquo;s privacy</h3>
            <p className="muted">
              The App is not directed at children and is not intended for use by children under
              13.
            </p>
          </div>

          <div>
            <h3>Changes to this policy</h3>
            <p className="muted">
              We may update this policy from time to time. Changes will be posted at this same
              URL with an updated &ldquo;Last updated&rdquo; date.
            </p>
          </div>

          <div>
            <h3>Contact us</h3>
            <p className="muted">
              Questions about this policy? Contact us at{" "}
              <a href="mailto:support@agrisenseandcontrol.in">support@agrisenseandcontrol.in</a>.
            </p>
          </div>
        </div>
      </section>
    </>
  );
}
