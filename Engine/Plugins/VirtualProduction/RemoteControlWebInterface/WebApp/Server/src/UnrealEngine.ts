import { IPayload, IPayloads, IPreset, IPanel, IView, ICustomStackWidget, ICustomStackTabs, PropertyValue, 
        IAsset, WidgetTypes, PropertyType } from '../../Client/src/shared';
import _ from 'lodash';
import WebSocket from 'ws';
import { Notify, Program } from './';
import request from 'superagent';
import crypto from 'crypto';


namespace UnrealApi {
  export enum PresetEvent {
    FieldsRenamed     = 'PresetFieldsRenamed',
    FieldsChanged     = 'PresetFieldsChanged',
    FieldsAdded       = 'PresetFieldsAdded',
    FieldsRemoved     = 'PresetFieldsRemoved',
    MetadataModified  = 'PresetMetadataModified',
    ActorModified     = 'PresetActorModified',
    EntitiesModified  = 'PresetEntitiesModified',
  }

  export type Presets = { 
    Presets: Partial<IPreset>[];
  };

  export type Preset = {
    Preset: IPreset;
  };

  export type View = {
    Value: string;
  };

  export type Request = {
    RequestId: number;
    URL: string;
    Verb: 'GET' | 'PUT' | 'POST' | 'DELETE';
  };

  export type Response = {
    RequestId: number;
    ResponseCode: number;
    ResponseBody: any;
  };

  export type BatchResponses = {
    Responses: Response[];
  };

  export type GetPropertyValue = {
    PropertyValue: PropertyValue;
  };

  export type PropertyValueSet = {
    ObjectPath: string;
    PropertyValue: PropertyValue;
  };

  export type PropertyValues = {
    PropertyValues: PropertyValueSet[];
  };

  export type Assets = {
    Assets: IAsset[];
  };

  export type RenameLabel = {
    AssignedLabel: string;
  };
}

export namespace UnrealEngine {
  let connection: WebSocket;
  let quitTimout: NodeJS.Timeout;

  let pendings: { [id: string]: (reply: any) => void } = {};
  let httpRequest: number = 1;
  let wsRequest: number = 1;

  let presets: IPreset[] = [];
  let payloads: IPayloads = {};
  let views: { [preset: string]: IView } = {};

  export async function initialize() {
    connect();
    setInterval(() => pullPresets(), 1000);
  }

  export function isConnected(): boolean {
    return (connection?.readyState === WebSocket.OPEN);
  }

  function connect() {
    if (connection?.readyState === WebSocket.OPEN || connection?.readyState === WebSocket.CONNECTING)
      return;

    const address = `ws://localhost:${Program.ueWebSocketPort}`;

    connection = new WebSocket(address);
    connection
      .on('open', onConnected)
      .on('message', onMessage)
      .on('error', onError)
      .on('close', onClose);
  }

  function onConnected() {
    if (connection.readyState !== WebSocket.OPEN)
      return;

    if (quitTimout) {
      clearTimeout(quitTimout);
      quitTimout = undefined;
    }
    
    console.log('Connected to UE Remote WebSocket');
    Notify.emit('connected', true);
    refresh();
  }

  async function refresh() {
    try {
      await pullPresets(true);
    } catch (error) {
    }
  }

  function onMessage(data: WebSocket.Data) {
    const json = data.toString();

    try {
      const message = JSON.parse(json);
      if (message.RequestId) {
        const promise = pendings[message.RequestId];
        if (!promise)
          return;

        delete pendings[message.RequestId];
        promise?.(message.ResponseBody);
        return;
      }

      switch (message.Type) {
        case UnrealApi.PresetEvent.FieldsChanged: {
          const preset = _.find(presets, p => p.Name === message.PresetName);
          if (!preset)
            break;

          for (const field of message.ChangedFields) {
            const property = _.find(preset.ExposedProperties, p => p.DisplayName === field.PropertyLabel); 
            if (!property)
              continue;

            setPayloadValueInternal(payloads, [message.PresetName, property.ID], field.PropertyValue);
            Notify.emitValueChange(message.PresetName, property.ID, field.PropertyValue);
          }
          break;
        }

        case UnrealApi.PresetEvent.FieldsAdded:
        case UnrealApi.PresetEvent.FieldsRemoved:
          refresh();
          break;

        case UnrealApi.PresetEvent.EntitiesModified:
          break;

        case UnrealApi.PresetEvent.MetadataModified:
          if (!message.Metadata.view)
            break;

          _.remove(presets, p => p.Name == message.PresetName);
          refresh()
            .then(() => refreshView(message.PresetName, message.Metadata.view));
          break;
      }

    } catch (error) {
      console.log('Failed to parse answer', error?.message, json);
    }
  }

  function onError(error: Error) {
    console.log('UE Remote WebSocket:', error.message);
  }

  function onClose() {
    presets = [];
    payloads = {};
    views = {};

    Notify.emit('connected', false);
    setTimeout(connect, 1000);

    if (Program.monitor && !quitTimout)
      quitTimout = setTimeout(quit, 15 * 1000);
  }

  function quit() {
    process.exit(1);
  }

  function verifyConnection() {
    if (connection?.readyState !== WebSocket.OPEN)
      throw new Error('Websocket is not connected');
  }

  function send(message: string, parameters: any) {
    verifyConnection();
    const Id = wsRequest++;
    connection.send(JSON.stringify({ MessageName: message, Id, Parameters: parameters }));
  }

  function http<T>(Verb: string, URL: string, Body?: object, wantAnswer?: boolean): Promise<T> {
    const RequestId = httpRequest++;
    send('http', { RequestId, Verb, URL, Body });
    if (!wantAnswer)
      return;

    return new Promise(resolve => {
      pendings[RequestId] = resolve;
    });
  }

  function get<T>(url: string): Promise<T> {
    return http<T>('GET', url, undefined, true);
  }

  function put<T>(url: string, body: object): Promise<T> {
    return http<T>('PUT', url, body, true);
  }

  function registerToPreset(PresetName: string): void {
    send('preset.register', { PresetName, IgnoreRemoteChanges: true });
  }

  function unregisterPreset(PresetName: string) {
    send('preset.unregister', { PresetName });
  }    

  export async function getPresets(): Promise<IPreset[]> {
    return presets;
  }

  async function pullPresets(pullValues?: boolean): Promise<void> {
    try {
      const allPresets = [];
      const allPayloads: IPayloads = {};
  
      const { Presets } = await get<UnrealApi.Presets>('/remote/presets');
      
      for (const p of Presets ?? []) {
        const Preset = await pullPreset(p.Name);
        if (!Preset)
          continue;

        allPresets.push(Preset);
        if (pullValues)
          allPayloads[Preset.Name] = await pullPresetValues(Preset);
      }

      const compact =  _.compact(allPresets);
      if (!equal(presets, compact)) {
        presets = compact;
        Notify.emit('presets', presets);
      }

      if (pullValues && !equal(payloads, allPayloads)) {
        payloads = allPayloads;
        Notify.emit('payloads', payloads );
      }

    } catch (error) {
      console.log('Failed to pull presets data');
    }
  }

  async function pullPreset(name: string): Promise<IPreset> {
    try {
      if (!_.find(presets, preset => preset.Name === name)) {
        registerToPreset(name);

        const res = await get<UnrealApi.View>(`/remote/preset/${name}/metadata/view`);
        refreshView(name, res?.Value);
      }

      const { Preset } = await get<UnrealApi.Preset>(`/remote/preset/${name}`);
      if (!Preset)
        return null;

      Preset.ExposedProperties = [];
      Preset.ExposedFunctions = [];
      Preset.Exposed = {};

      for (const Group of Preset.Groups) {
        for (const Property of Group.ExposedProperties) {
          Property.Type = Property.UnderlyingProperty.Type;
          Preset.Exposed[Property.ID] = Property;
        }

        for (const Function of Group.ExposedFunctions) {
          if (!Function.Metadata)
            Function.Metadata = {};

          Function.Type = PropertyType.Function;
          Function.Metadata.Widget = WidgetTypes.Button;
          Preset.Exposed[Function.ID] = Function;
        }

        Preset.ExposedProperties.push(...Group.ExposedProperties);
        Preset.ExposedFunctions.push(...Group.ExposedFunctions);
      }

      return Preset;
    } catch (error) {
      console.log(`Failed to pull preset '${name}' data`);
    }
  }

  async function refreshView(preset: string, viewJson: string) {
    try {
      if (!viewJson)
        return;

      const view = JSON.parse(viewJson) as IView;
      if (equal(view, views[preset]))
        return; 

      for (const tab of view.tabs) {
        if (!tab.panels)
          continue;

        for (const panel of tab.panels)
          setPanelIds(panel);
      }

      views[preset] = view;
      Notify.onViewChange(preset, view, true);
    } catch (error) {
      console.log('Failed to parse View of Preset', preset);
    }    
  }

  function setPanelIds(panel: IPanel) {
    if (!panel.id)
      panel.id = crypto.randomBytes(16).toString('hex');

    if (panel.widgets)
      return setWidgetsId(panel.widgets);

    if (panel.items)
      for (const item of panel.items) {
        if (!item.id)
          item.id = crypto.randomBytes(16).toString('hex');

        for (const panel of item.panels)
          setPanelIds(panel);
      }
  }

  function setWidgetsId(widgets?: ICustomStackWidget[]) {
    if (!widgets)
      return;

    for (const widget of widgets) {
      if (!widget.id)
        widget.id = crypto.randomBytes(16).toString('hex');

      if (widget.widget === 'Tabs') {
        const tabWidget = widget as ICustomStackTabs;
        for (const tab of tabWidget?.tabs ?? []) {
          if (!tab.id)
            tab.id = crypto.randomBytes(16).toString('hex');

          setWidgetsId(tab.widgets);
        }
      }
    }
  }

  function equal(a: any, b: any): boolean {
    if (a === b)
      return true;
    
    if (!!a !== !!b || typeof(a) !== typeof(b))
      return false;

    if (Array.isArray(a) && Array.isArray(b)) {
      if (a.length !== b.length)
        return false;

      for (let i = 0; i < a.length; i++) {
        if (!equal(a[i], b[i]))
          return false;
      }

      return true;
    }

    if (typeof(a) === 'object' && typeof(b) === 'object') {
      if (!equal(Object.keys(a), Object.keys(b)))
        return false;

      for (const key in a) {
        if (!equal(a[key], b[key]))
          return false;
      }

      return true;
    }

    return false;
  }

  async function pullPresetValues(Preset: IPreset): Promise<IPayload> {
    const updatedPayloads: IPayloads = {};
    for (const property of Preset.ExposedProperties) {
      const value = await get<UnrealApi.PropertyValues>(`/remote/preset/${Preset.Name}/property/${property.ID}`);
      setPayloadValueInternal(updatedPayloads, [Preset.Name, property.ID], value?.PropertyValues?.[0]?.PropertyValue);
    }

    return updatedPayloads[Preset.Name];
  }

  export async function getPayload(preset: string): Promise<IPayload> {
    return payloads[preset];
  }

  export async function getPayloads(): Promise<IPayloads> {
    return { ...payloads };
  }

  export async function setPayload(preset: string, payload: IPayload): Promise<void> {
    payloads[preset] = payload;
  }

  export async function setPayloadValue(preset: string, property: string, value: PropertyValue): Promise<void> {
    try {
      const body: any = { GenerateTransaction: true };
      if (value !== null) {
        // setPayloadValueInternal(payloads, [preset, property], value);
        // Notify.emitValueChange(preset, property, value);
        body.PropertyValue = value;
      } else {
        body.ResetToDefault = true; 
      }

      await put(`/remote/preset/${preset}/property/${property}`, body);

      // if (value === null) {
      //   const ret = await get<UnrealApi.PropertyValues>(`/remote/preset/${preset}/property/${property}`);
      //   value = ret.PropertyValues?.[0]?.PropertyValue;
      //   if (value !== undefined) {
      //     setPayloadValueInternal(payloads, [preset, property], value);
      //     Notify.emitValueChange(preset, property, value);
      //   }
      // }
    } catch (err) {
      console.log('Failed to set preset data:', err.message);
    }
  }

  function setPayloadValueInternal(data: IPayloads, path: string[], value: PropertyValue): void {
    if (!data || !path.length)
      return;

    let element: any = data;
    for (let i = 0; i < path.length - 1; i++) {
      const property = path[i];
      if (!element[property])
        element[property] = {};
      
      element = element[property];
    }

    element[ _.last(path) ] = value;
  }

  export async function executeFunction(preset: string, func: string, args: Record<string, any>): Promise<void> {
    try {
      const url = `/remote/preset/${preset}/function/${func}`;
      await put(url, { Parameters: args, GenerateTransaction: true });
    } catch (err) {
      console.log('Failed to set execute function call:', err.message);
    }
  }

  export async function setPresetPropertyMetadata(preset: string, property: string, metadata: string, value: string) {
    try {

      const url = `/remote/preset/${preset}/property/${property}/metadata/${metadata}`;
      await put(url, { value });
      const p = _.find(presets, p => p.Name === preset);
      const prop = p?.Exposed[property];
      if (prop) {
        prop.Metadata[metadata] = value;
        Notify.emit('presets', presets);
      }
    } catch (err) {
      console.log(`Failed to set property metadata`);
    }
  }

  export async function getView(preset: string): Promise<IView> {
    return views[preset];
  }

  export async function setView(preset: string, view: IView): Promise<void> {
    for (const tab of view.tabs) {
      if (!tab.panels)
        continue;

      for (const panel of tab.panels)
        setPanelIds(panel);
    }

    views[preset] = view;
    const Value = JSON.stringify(view);
    await put(`/remote/preset/${preset}/metadata/view`, { Value });
  }

  export async function search(query: string, types: string[], prefix: string, count: number): Promise<IAsset[]> {
    const ret = await put<UnrealApi.Assets>('/remote/search/assets', {
      Query: query,
      Limit: count,
      Filter: {
        ClassNames: types,
        PackagePaths: [prefix],
        RecursivePaths: true
      }
    });

    return ret.Assets;
  }

  export function proxy(method: 'GET' | 'PUT', url: string, body?: any): Promise<any> {
    if (!method || !url)
      return Promise.resolve({});

    switch (method) {
      case 'GET':
        return get(url);

      case 'PUT':
        if (body)
          return put(url, body);
        break;
    }

    return Promise.resolve({});
  }

  export function thumbnail(asset: string): Promise<any> {
    return request.put(`http://localhost:${Program.ueHttpPort}/remote/object/thumbnail`)
                  .send({ ObjectPath: asset })
                  .then(res => res.body);
  }
}